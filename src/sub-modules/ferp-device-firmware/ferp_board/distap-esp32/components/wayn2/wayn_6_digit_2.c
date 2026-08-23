#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sys_types.h"
#include "board_io.h"
#include "device.h"
#include "wayn_6_digit_2.h"

// Wayne Type02 display bus.
//
// Unlike the older wayn_6_digit.c (edge-counted GPIO bit-bang, MSB-first,
// single continuous 16-byte burst), this pump uses a real CS-framed,
// LSB-first, 95-byte-per-refresh-cycle protocol captured via hardware SPI
// slave — modeled on censtar_7cs_digit.c's proven pattern: the pump has no
// real CS line, so an RCLK-silence timer synthesizes one via the board's
// D1_OUT_CS -> D1_IN_CS loopback.
//
// A full cycle is ~95 bytes but we only need the first ~22:
//   bytes  0-1   : 0xFF 0xFF sync
//   bytes  2-7    : Volume digits (main table, LSB-first)
//   bytes  8-13   : Total digits (main table, LSB-first)
//   byte   14     : unknown constant (0x20)
//   byte   15     : Group-A address (0x10, first of six repeated addr+payload
//                   pairs carrying Rate — always 0x10 since we only ever
//                   capture the FIRST occurrence)
//   bytes  16-21  : Rate payload — a DIFFERENT segment table (bit7 = DP,
//                   standard 7-seg hex codes) from Volume/Total's table.
//                   Only the first 4 bytes (16-19) are meaningful digits
//                   (Rate is a 4-digit, 1-decimal value); bytes 20-21 don't
//                   match this table and are not yet understood — not
//                   needed since Rate's 4 digits are already captured.
//
// Rather than capturing the whole ~95-byte cycle (bounding the SPI
// transaction on the pump's own end-of-cycle silence, like Censtar does),
// we fix the transaction length to CAPTURE_LEN_BYTES so it completes on
// its own the moment that many bytes are clocked in — well before the
// pump reaches the parts of the cycle we don't care about (rest of Group
// A, Group B, tail). CS is then explicitly raised by us (not by the
// pump's silence, since the pump keeps clocking well past this point) to
// cleanly end the transaction before waiting for the next genuine
// inter-cycle gap to re-arm.

#define D1_SPI_HOST SPI2_HOST
#define D2_SPI_HOST SPI3_HOST

#define CAPTURE_LEN_BYTES 32  // main block (15B) + Group-A addr+payload (7B) + margin
#define DIGIT_GROUP_LEN   6   // Volume/Total digit count
#define RATE_DIGIT_LEN    4   // Rate significant-digit count (tenths..hundreds)

#define RATE_ADDR_OFFSET    15
#define RATE_PAYLOAD_OFFSET 16
#define RATE_ADDR_EXPECTED  0x10

// Measured on Wayne Type02 ground-truth captures: intra-frame gaps stay
// under ~1ms, inter-frame (cycle boundary) gaps are 50-109ms — 10ms sits
// with ~10x margin on both sides.
#define TIMER_TOUT_US         (10 * 1000)  // RCLK-silence -> safe to arm
#define TIMER_TOUT_CAPTURE_US (150 * 1000) // watchdog if the pump never clocks the window

typedef struct
{
    spi_host_device_t   spi_host;
    gpio_num_t           out_cs_pin;
    tx_pckt_id_t         tx_pck_id;
    const char          *tag;

    uint8_t             *recvbuf;
    esp_timer_handle_t   silence_timer;
    esp_timer_handle_t   watchdog_timer;
    volatile bool        cs_level; // set true by RCLK-silence timer -> safe to arm
    volatile bool        cs_tout;  // set true by watchdog timer -> capture stuck

    uint32_t             n_attempts;
    uint32_t             n_cstout;
    uint32_t             n_short;
    uint32_t             n_sync_fail;
    uint32_t             n_digit_fail;
    uint32_t             n_ok;
} wayn2_line_ctx_t;

WORD_ALIGNED_ATTR static uint8_t dis1_recvbuf[CAPTURE_LEN_BYTES] = {};
WORD_ALIGNED_ATTR static uint8_t dis2_recvbuf[CAPTURE_LEN_BYTES] = {};

static esp_timer_handle_t dis1_timer = NULL, dis1_timer_cs = NULL;
static esp_timer_handle_t dis2_timer = NULL, dis2_timer_cs = NULL;

static wayn2_line_ctx_t ctx_dis1;
static wayn2_line_ctx_t ctx_dis2;

static QueueHandle_t *ptr_send_que = NULL;

static void task_line_capture(void *arg);

IRAM_ATTR static void timer_cs_tout_dis1(void *arg)
{
    gpio_set_level(D1_OUT_CS, true);
    ctx_dis1.cs_tout = true;
}
IRAM_ATTR static void timer_rclk_tout_dis1(void *arg)
{
    gpio_set_level(D1_OUT_CS, true);
    ctx_dis1.cs_level = true;
}
IRAM_ATTR static void gpio_rclk_done_dis1(void *arg)
{
    if (esp_timer_is_active(dis1_timer))
        esp_timer_restart(dis1_timer, TIMER_TOUT_US);
    else
        esp_timer_start_once(dis1_timer, TIMER_TOUT_US);
}

IRAM_ATTR static void timer_cs_tout_dis2(void *arg)
{
    gpio_set_level(D2_OUT_CS, true);
    ctx_dis2.cs_tout = true;
}
IRAM_ATTR static void timer_rclk_tout_dis2(void *arg)
{
    gpio_set_level(D2_OUT_CS, true);
    ctx_dis2.cs_level = true;
}
IRAM_ATTR static void gpio_rclk_done_dis2(void *arg)
{
    if (esp_timer_is_active(dis2_timer))
        esp_timer_restart(dis2_timer, TIMER_TOUT_US);
    else
        esp_timer_start_once(dis2_timer, TIMER_TOUT_US);
}

// Table 1 — Volume/Total digit strip. Decimal point is bit0.
// Digits 4/5/6 carried over from the old wayn_6_digit.c table — UNVERIFIED
// against real Type02 captures. Digit 9 (0xE6) is confirmed for Type02
// (differs from the old table's 0xF6).
static bool segment_to_digit_main(uint8_t raw, uint8_t *digit)
{
    switch (raw & 0xFE)
    {
        case 0xFC: *digit = 0; return true;
        case 0x60: *digit = 1; return true;
        case 0xDA: *digit = 2; return true;
        case 0xF2: *digit = 3; return true;
        case 0x66: *digit = 4; return true; // unverified for Type02
        case 0xB6: *digit = 5; return true; // unverified for Type02
        case 0xBE: *digit = 6; return true; // unverified for Type02
        case 0xE0: *digit = 7; return true;
        case 0xFE: *digit = 8; return true;
        case 0xE6: *digit = 9; return true;
        case 0x00: *digit = 0; return true; // blanked position
        default:   return false;
    }
}

// Table 2 — Rate sub-display (Group A payload). Different physical
// element from Volume/Total, using the industry-standard 7-segment hex
// codes with decimal point on bit7. Confirmed against a live capture:
// 0x3F,0xCF,0x06,0x4F -> 0,3.,1,3 -> 313.0.
static bool segment_to_digit_rate(uint8_t raw, uint8_t *digit)
{
    switch (raw & 0x7F)
    {
        case 0x3F: *digit = 0; return true;
        case 0x06: *digit = 1; return true;
        case 0x5B: *digit = 2; return true;
        case 0x4F: *digit = 3; return true;
        case 0x66: *digit = 4; return true;
        case 0x6D: *digit = 5; return true;
        case 0x7D: *digit = 6; return true;
        case 0x07: *digit = 7; return true;
        case 0x7F: *digit = 8; return true;
        case 0x6F: *digit = 9; return true;
        default:   return false;
    }
}

// Decodes `len` LSB-first digit bytes into a raw natural-scale value (the
// plain integer the display shows, decimal point stripped — e.g. Volume
// "000.127" -> 127, Rate "313.0" -> 3130). Deliberately NOT rescaled to
// the canonical x100/x1000 convention here — per the existing pattern in
// this codebase (see DIS_CENSTAR_6_DIGIT), that correction belongs in
// fuel_types_from_frame() on the main-esp32 side.
static bool decode_digit_group(const uint8_t *group, uint8_t len,
                                bool (*lut)(uint8_t, uint8_t *), uint32_t *value)
{
    uint32_t v = 0, weight = 1;
    for (uint8_t i = 0; i < len; i++)
    {
        uint8_t digit;
        if (!lut(group[i], &digit))
            return false;
        v += (uint32_t)digit * weight;
        weight *= 10;
    }
    *value = v;
    return true;
}

// -1 = sync/address mismatch, 0 = digit decode failure, 1 = ok
static int decode_frame(const uint8_t *buf, display_data_t *dd)
{
    if (buf[0] != 0xFF || buf[1] != 0xFF)
        return -1;
    if (buf[RATE_ADDR_OFFSET] != RATE_ADDR_EXPECTED)
        return -1;

    uint32_t volume, total, rate;
    if (!decode_digit_group(&buf[2], DIGIT_GROUP_LEN, segment_to_digit_main, &volume))
        return 0;
    if (!decode_digit_group(&buf[8], DIGIT_GROUP_LEN, segment_to_digit_main, &total))
        return 0;
    if (!decode_digit_group(&buf[RATE_PAYLOAD_OFFSET], RATE_DIGIT_LEN, segment_to_digit_rate, &rate))
        return 0;

    memset(dd, 0, sizeof(*dd));
    dd->volume_l    = volume;
    dd->total_price = total;
    dd->unit_price  = rate; // raw natural scale, 1 decimal — same convention as total_price
    return 1;
}

static void log_hexdump(const char *tag, const uint8_t *buf, size_t len)
{
    char line[3 * CAPTURE_LEN_BYTES + 1] = {0};
    size_t n = len < CAPTURE_LEN_BYTES ? len : CAPTURE_LEN_BYTES;
    for (size_t i = 0; i < n; i++)
        sprintf(&line[i * 3], "%02X ", buf[i]);
    ESP_LOGW(tag, "%u bytes: %s", (unsigned)n, line);
}

static void task_line_capture(void *arg)
{
    wayn2_line_ctx_t *c = (wayn2_line_ctx_t *)arg;
    esp_err_t ret;
    display_data_t capture_now = {};
    display_data_t cap_data = {};
    data_packet_t display_data = {
        .display = DIS_WAYNE_6_DIGIT_2,
        .length = sizeof(display_data_t)};
    TickType_t ticks_last = xTaskGetTickCount();
    TickType_t ticks_last_stats = xTaskGetTickCount();

    spi_slave_transaction_t trans = {
        .length = CAPTURE_LEN_BYTES * 8,
        .rx_buffer = c->recvbuf,
    };

    gpio_set_level(c->out_cs_pin, true); // idle — not listening yet
    c->cs_level = false;

    while (1)
    {
        if (!c->cs_level)
        {
            vTaskDelay(1);
            goto stats_and_resend;
        }

        c->cs_tout = false;
        trans.trans_len = 0;
        esp_timer_stop(c->silence_timer);
        gpio_set_level(c->out_cs_pin, false); // arm — confirmed fresh cycle boundary
        esp_timer_start_once(c->watchdog_timer, TIMER_TOUT_CAPTURE_US);

        ret = spi_slave_transmit(c->spi_host, &trans, pdMS_TO_TICKS(2 * 1000));

        esp_timer_stop(c->watchdog_timer);
        gpio_set_level(c->out_cs_pin, true); // done — explicit clean end, ignore rest of cycle
        c->cs_level = false;

        c->n_attempts++;

        {
            size_t got_bytes = trans.trans_len / 8;
            if (c->cs_tout || ret != ESP_OK || got_bytes < CAPTURE_LEN_BYTES)
            {
                if (c->cs_tout) c->n_cstout++;
                else c->n_short++;
                ESP_LOGI(c->tag, "cstout:%d fail:0x%.2x len:%u", c->cs_tout, ret, (unsigned)got_bytes);
                goto stats_and_resend;
            }
        }

        {
            int r = decode_frame(c->recvbuf, &capture_now);
            if (r <= 0)
            {
                if (r == -1) { c->n_sync_fail++; ESP_LOGW(c->tag, "sync/addr mismatch"); }
                else         { c->n_digit_fail++; ESP_LOGW(c->tag, "digit decode error"); }
                log_hexdump(c->tag, c->recvbuf, CAPTURE_LEN_BYTES);
                goto stats_and_resend;
            }
        }

        c->n_ok++;
        {
            TickType_t ticks_now = xTaskGetTickCount();
            if (ticks_now - ticks_last > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) || memcmp(&cap_data, &capture_now, sizeof(display_data_t)))
            {
                ticks_last = ticks_now;
                display_data.pck_id = c->tx_pck_id;
                memcpy(display_data.ab_data, &capture_now, sizeof(display_data_t));
                memcpy(&cap_data, &capture_now, sizeof(display_data_t));
                xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
            }
        }

    stats_and_resend:
        {
            TickType_t ticks_now = xTaskGetTickCount();
            if ((ticks_now - ticks_last) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
            {
                ticks_last = ticks_now;
                display_data.pck_id = c->tx_pck_id;
                memcpy(display_data.ab_data, &cap_data, sizeof(display_data_t));
                xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
            }
            if ((ticks_now - ticks_last_stats) > pdMS_TO_TICKS(5000))
            {
                ticks_last_stats = ticks_now;
                ESP_LOGI(c->tag, "stats: attempts=%lu ok=%lu cstout=%lu short=%lu sync_fail=%lu digit_fail=%lu",
                          (unsigned long)c->n_attempts, (unsigned long)c->n_ok, (unsigned long)c->n_cstout,
                          (unsigned long)c->n_short, (unsigned long)c->n_sync_fail, (unsigned long)c->n_digit_fail);
            }
        }
    }
    vTaskDelete(NULL);
}

esp_err_t display_wayne_6_digit_2_init(QueueHandle_t *send_queue)
{
    esp_err_t ret = ESP_OK;
    gpio_config_t io_conf;

    // display enable + synthesized CS outputs (D1_OUT_CS/D2_OUT_CS are
    // wired back to D1_IN_CS/D2_IN_CS on this board — see censtar_7cs_digit.c)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = BIT64(DIS_ENB) | BIT64(D1_OUT_CS) | BIT64(D2_OUT_CS);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    // RCLK inputs — used only for silence timing, never wired directly
    // into the SPI hardware. ANYEDGE so the "still active" detector works
    // regardless of the bus's actual idle polarity.
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(D1_RCLK) | BIT64(D2_RCLK);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    // SDATA2 unused by this display type
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(D1_SDATA2) | BIT64(D2_SDATA2);
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    ESP_ERROR_GOTO(ret, end, gpio_install_isr_service(0));

    ESP_ERROR_GOTO(ret, end, spi_slave_initialize(D1_SPI_HOST, &(const spi_bus_config_t){
        .sclk_io_num = D1_SCLK,
        .mosi_io_num = D1_SDATA1,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
    }, &(const spi_slave_interface_config_t){
        .mode = 0, // CPOL=0, CPHA=0 — measured on the real Wayne Type02 bus
        .spics_io_num = D1_IN_CS,
        .queue_size = 4,
        .flags = 0,
    }, SPI_DMA_CH1));

    ESP_ERROR_GOTO(ret, end, spi_slave_initialize(D2_SPI_HOST, &(const spi_bus_config_t){
        .sclk_io_num = D2_SCLK,
        .mosi_io_num = D2_SDATA1,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
    }, &(const spi_slave_interface_config_t){
        .mode = 0,
        .spics_io_num = D2_IN_CS,
        .queue_size = 4,
        .flags = 0,
    }, SPI_DMA_CH2));

    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "t2_dis_1",
        .callback = timer_rclk_tout_dis1,
    }, &dis1_timer));
    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "t2_cs_dis_1",
        .callback = timer_cs_tout_dis1,
    }, &dis1_timer_cs));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D1_RCLK, gpio_rclk_done_dis1, NULL));

    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "t2_dis_2",
        .callback = timer_rclk_tout_dis2,
    }, &dis2_timer));
    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "t2_cs_dis_2",
        .callback = timer_cs_tout_dis2,
    }, &dis2_timer_cs));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D2_RCLK, gpio_rclk_done_dis2, NULL));

    ptr_send_que = send_queue;

    ctx_dis1 = (wayn2_line_ctx_t){
        .spi_host = D1_SPI_HOST, .out_cs_pin = D1_OUT_CS,
        .tx_pck_id = TX_ID_DIS1_DATA, .tag = "wayn2.d1",
        .recvbuf = dis1_recvbuf, .silence_timer = dis1_timer, .watchdog_timer = dis1_timer_cs,
    };
    ctx_dis2 = (wayn2_line_ctx_t){
        .spi_host = D2_SPI_HOST, .out_cs_pin = D2_OUT_CS,
        .tx_pck_id = TX_ID_DIS2_DATA, .tag = "wayn2.d2",
        .recvbuf = dis2_recvbuf, .silence_timer = dis2_timer, .watchdog_timer = dis2_timer_cs,
    };

    if (xTaskCreate(task_line_capture, "task_wayn2_d1", 8 * 1024, &ctx_dis1, 8, NULL) == pdFALSE)
    {
        ret = ESP_FAIL;
        goto end;
    }
    if (xTaskCreate(task_line_capture, "task_wayn2_d2", 8 * 1024, &ctx_dis2, 8, NULL) == pdFALSE)
    {
        ret = ESP_FAIL;
        goto end;
    }

    gpio_set_level(DIS_ENB, true);
    ESP_LOGI("wayn2", "Starting Wayne Type02 display capture\r\n");
end:
    return ret;
}
