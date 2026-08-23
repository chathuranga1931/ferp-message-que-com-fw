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
// Capture mechanism validated empirically via the DIS_RAW_SPI_V1 (type 92)
// diagnostic tool before being applied here — see that tool's header
// comment (raw_capture_spi.c) for the full derivation. Summary:
//
//   1. RCLK falling edge restarts a ~45ms "wait" timer.
//   2. Once 45ms passes with no further falling edge, the RCLK interrupt
//      is disabled (no interference once capturing has started) and CS is
//      driven low (arm) via the D1_OUT_CS -> D1_IN_CS board loopback (the
//      pump has no real CS line, so this is a synthesized one). A
//      separate, fixed ~50ms "capture window" timer starts — NOT reset
//      by any further RCLK activity.
//   3. When the capture-window timer fires, CS is driven high — this ends
//      the pending SPI-slave transaction (completion on classic ESP32 is
//      tied to CS, not to how many bytes were clocked in). The real frame
//      completes in ~28-38ms, so 50ms leaves comfortable margin without
//      wasting the extra ~60ms the type-92 diagnostic used (that tool
//      wanted to see the *whole* cycle; the decoder only needs to reach
//      the Rate payload ~18 bytes in) — every ms of unnecessary window
//      length is a ms this line can't attempt a fresh capture.
//   4. The captured buffer is decoded, the RCLK interrupt is re-enabled,
//      and the line goes back to waiting for the next falling edge.
//
// Empirically confirmed offset: because CS asserts a fixed ~45ms after
// the bus goes quiet rather than being bound to the pump's own signal,
// capture consistently starts ~2 bytes into the frame — missing the
// leading 0xFF 0xFF sync, landing right on the first Volume digit
// instead. This is fixed and repeatable, not random, so the decoder
// below reads from that confirmed offset directly rather than searching
// for a sync that's never actually captured.
//
// Frame layout AS CAPTURED (buffer offset 0 = frame byte 2):
//   bytes  0-5   : Volume digits (main table, LSB-first)
//   bytes  6-11  : Total digits (main table, LSB-first)
//   byte   12    : unknown constant (0x20)
//   byte   13    : Group-A address (0x10, first of six repeated addr+payload
//                  pairs carrying Rate — always 0x10 since this is always
//                  the first occurrence right after the main block)
//   bytes  14-17 : Rate payload (first 4 of 6 bytes) — a DIFFERENT segment
//                  table (bit7 = DP, standard 7-seg hex codes) from
//                  Volume/Total's table. Rate is a 4-digit, 1-decimal
//                  value; the remaining 2 bytes of the 6-byte payload
//                  don't match this table and are not understood yet.

#define D1_SPI_HOST SPI2_HOST
#define D2_SPI_HOST SPI3_HOST

#define RECVBUF_SIZE      128 // comfortably above one full ~95-byte cycle
#define MIN_CAPTURE_BYTES 18  // Volume(6) + Total(6) + extra(1) + addr(1) + Rate(4)
#define DIGIT_GROUP_LEN   6   // Volume/Total digit count
#define RATE_DIGIT_LEN    4   // Rate significant-digit count (tenths..hundreds)

// Offsets as captured — see header comment; NOT frame-relative offsets,
// already adjusted for the confirmed 2-byte-early start.
#define VOLUME_OFFSET  0
#define TOTAL_OFFSET   6
#define EXTRA_OFFSET   12
#define RATE_ADDR_OFFSET    13
#define RATE_PAYLOAD_OFFSET 14
#define RATE_ADDR_EXPECTED  0x10
#define EXTRA_EXPECTED      0x20

// Measured on Wayne Type02 ground-truth captures: intra-frame gaps stay
// under ~1ms, inter-frame (cycle boundary) gaps are 50-109ms. 45ms sits
// comfortably in between; validated against real hardware via the type-92
// diagnostic tool (consistently reproduced the known-correct frame).
#define WAIT_TIMER_US     (45 * 1000) // RCLK-silence -> safe to arm
// The type-92 diagnostic used 100ms to see the *whole* cycle with margin
// to spare (frame completed in ~28-38ms; the rest was idle since the pump's
// SCLK goes quiet during its own inter-cycle gap). For the real decoder we
// only need to reach the Rate payload (~18 bytes in), so 50ms keeps ample
// margin over the observed frame length while roughly halving the total
// attempt cycle (wait + window) — closer to how often Censtar/etc. get a
// fresh capture, since each attempt cycle is a hard floor on update rate
// regardless of the DIFF_PCKT_SEND_MS/SAME_PCKT_SEND_MS send pacing below.
#define CAPTURE_WINDOW_US (100 * 1000) // fixed capture-open duration, not RCLK-gated

typedef struct
{
    spi_host_device_t   spi_host;
    gpio_num_t           rclk_pin;
    gpio_num_t           out_cs_pin;
    tx_pckt_id_t         tx_pck_id;
    const char          *tag;

    uint8_t             *recvbuf;
    esp_timer_handle_t   wait_timer;
    esp_timer_handle_t   capture_timer;
    TaskHandle_t         task; // notified by wait_timer callback -> task should start capturing

    uint32_t             n_attempts;
    uint32_t             n_short;
    uint32_t             n_addr_fail;
    uint32_t             n_digit_fail;
    uint32_t             n_ok;
} wayn2_line_ctx_t;

WORD_ALIGNED_ATTR static uint8_t dis1_recvbuf[RECVBUF_SIZE] = {};
WORD_ALIGNED_ATTR static uint8_t dis2_recvbuf[RECVBUF_SIZE] = {};

static esp_timer_handle_t dis1_wait_timer = NULL, dis1_capture_timer = NULL;
static esp_timer_handle_t dis2_wait_timer = NULL, dis2_capture_timer = NULL;

static wayn2_line_ctx_t ctx_dis1;
static wayn2_line_ctx_t ctx_dis2;

static QueueHandle_t *ptr_send_que = NULL;

static void task_line_capture(void *arg);

static void wait_timer_cb_dis1(void *arg)
{
    gpio_intr_disable(D1_RCLK);
    if (ctx_dis1.task) xTaskNotifyGive(ctx_dis1.task);
}
IRAM_ATTR static void capture_timer_cb_dis1(void *arg)
{
    gpio_set_level(D1_OUT_CS, true);
}
IRAM_ATTR static void gpio_rclk_negedge_dis1(void *arg)
{
    if (esp_timer_is_active(dis1_wait_timer))
        esp_timer_restart(dis1_wait_timer, WAIT_TIMER_US);
    else
        esp_timer_start_once(dis1_wait_timer, WAIT_TIMER_US);
}

static void wait_timer_cb_dis2(void *arg)
{
    gpio_intr_disable(D2_RCLK);
    if (ctx_dis2.task) xTaskNotifyGive(ctx_dis2.task);
}
IRAM_ATTR static void capture_timer_cb_dis2(void *arg)
{
    gpio_set_level(D2_OUT_CS, true);
}
IRAM_ATTR static void gpio_rclk_negedge_dis2(void *arg)
{
    if (esp_timer_is_active(dis2_wait_timer))
        esp_timer_restart(dis2_wait_timer, WAIT_TIMER_US);
    else
        esp_timer_start_once(dis2_wait_timer, WAIT_TIMER_US);
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
// codes with decimal point on bit7. Confirmed against live captures:
// 0x3F,0xCF,0x06,0x4F -> 0,3.,1,3 -> 313.0; 0x3F,0x86,0x3F,0x3F -> 0,1.,0,0 -> 1.0.
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

// -1 = Group-A address mismatch (misaligned capture), 0 = digit decode
// failure, 1 = ok
static int decode_frame(const uint8_t *buf, display_data_t *dd)
{
    if (buf[EXTRA_OFFSET] != EXTRA_EXPECTED || buf[RATE_ADDR_OFFSET] != RATE_ADDR_EXPECTED)
        return -1;

    uint32_t volume, total, rate;
    if (!decode_digit_group(&buf[VOLUME_OFFSET], DIGIT_GROUP_LEN, segment_to_digit_main, &volume))
        return 0;
    if (!decode_digit_group(&buf[TOTAL_OFFSET], DIGIT_GROUP_LEN, segment_to_digit_main, &total))
        return 0;
    if (!decode_digit_group(&buf[RATE_PAYLOAD_OFFSET], RATE_DIGIT_LEN, segment_to_digit_rate, &rate))
        return 0;

    memset(dd, 0, sizeof(*dd));
    dd->volume_l    = volume;
    dd->total_price = total;
    dd->unit_price  = rate; // raw natural scale, 1 decimal — same convention as total_price
    return 1;
}

// Remote-visible diagnostics: this DT board is often unreachable directly
// (no local console access), so failures are relayed to main-esp32 via
// LOG_PRINT (TX_ID_LOG_PRINTS) in addition to the local ESP_LOG* output.
static void log_hexdump_remote(const char *tag, const uint8_t *buf, size_t len)
{
    char line[3 * MIN_CAPTURE_BYTES + 1] = {0};
    size_t n = len < MIN_CAPTURE_BYTES ? len : MIN_CAPTURE_BYTES;
    for (size_t i = 0; i < n; i++)
        sprintf(&line[i * 3], "%02X ", buf[i]);
    ESP_LOGW(tag, "%u bytes: %s", (unsigned)n, line);
    LOG_PRINT("%s %u bytes: %s\r\n", tag, (unsigned)n, line);
}

static void task_line_capture(void *arg)
{
    wayn2_line_ctx_t *c = (wayn2_line_ctx_t *)arg;
    c->task = xTaskGetCurrentTaskHandle();

    display_data_t capture_now = {};
    display_data_t cap_data = {};
    data_packet_t display_data = {
        .display = DIS_WAYNE_6_DIGIT_2,
        .length = sizeof(display_data_t)};
    TickType_t ticks_last = xTaskGetTickCount();
    TickType_t ticks_last_stats = xTaskGetTickCount();

    spi_slave_transaction_t trans = {
        .length = RECVBUF_SIZE * 8,
        .rx_buffer = c->recvbuf,
    };

    gpio_set_level(c->out_cs_pin, true); // idle — not listening yet

    while (1)
    {
        // RCLK interrupt is OFF from here until just before this point next
        // time around, spanning the whole capture+decode+send below —
        // mirrors the type-92 diagnostic tool exactly (see raw_capture_spi.c
        // for why this matters: re-enabling too early lets a notification
        // go stale while other work is still pending).
        (void)ulTaskNotifyTake(pdTRUE, 0); // discard any stale count before re-enabling
        gpio_intr_enable(c->rclk_pin);

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // blocks until wait_timer fires (no polling jitter)

        memset(c->recvbuf, 0xEE, RECVBUF_SIZE); // sentinel — distinguishes "nothing captured" from real zeros
        trans.trans_len = 0;

        gpio_set_level(c->out_cs_pin, false); // CS low — arm
        esp_timer_start_once(c->capture_timer, CAPTURE_WINDOW_US);

        esp_err_t ret = spi_slave_transmit(c->spi_host, &trans, pdMS_TO_TICKS(1000));

        esp_timer_stop(c->capture_timer);
        gpio_set_level(c->out_cs_pin, true); // ensure high (capture_timer should already have done this)

        c->n_attempts++;

        size_t got_bytes = trans.trans_len / 8;
        if (ret != ESP_OK || got_bytes < MIN_CAPTURE_BYTES)
        {
            c->n_short++;
            #ifdef LOG_OUT
            ESP_LOGI(c->tag, "fail:0x%.2x len:%u", ret, (unsigned)got_bytes);
            LOG_PRINT("%s fail:0x%.2x len:%u\r\n", c->tag, ret, (unsigned)got_bytes);
            #endif
            goto next;
        }

        {
            int r = decode_frame(c->recvbuf, &capture_now);
            if (r <= 0)
            {
                if (r == -1) { c->n_addr_fail++; ESP_LOGW(c->tag, "addr mismatch (misaligned capture)"); }
                else         { c->n_digit_fail++; ESP_LOGW(c->tag, "digit decode error"); }
                log_hexdump_remote(c->tag, c->recvbuf, got_bytes);
                goto next;
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

    next:
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
                #ifdef LOG_OUT
                ESP_LOGI(c->tag, "stats: attempts=%lu ok=%lu short=%lu addr_fail=%lu digit_fail=%lu",
                          (unsigned long)c->n_attempts, (unsigned long)c->n_ok, (unsigned long)c->n_short,
                          (unsigned long)c->n_addr_fail, (unsigned long)c->n_digit_fail);
                LOG_PRINT("%s stats: att=%lu ok=%lu short=%lu addr=%lu dig=%lu\r\n",
                          c->tag, (unsigned long)c->n_attempts, (unsigned long)c->n_ok, (unsigned long)c->n_short,
                          (unsigned long)c->n_addr_fail, (unsigned long)c->n_digit_fail);
                #endif
            }
        }
    }
    vTaskDelete(NULL);
}

esp_err_t display_wayne_6_digit_2_init(QueueHandle_t *send_queue)
{
    esp_err_t ret = ESP_OK;
    gpio_config_t io_conf;

    // Populate contexts BEFORE installing the RCLK ISR / creating timers,
    // so a stray RCLK edge during setup can never touch an unpopulated ctx.
    ctx_dis1 = (wayn2_line_ctx_t){
        .spi_host = D1_SPI_HOST, .rclk_pin = D1_RCLK, .out_cs_pin = D1_OUT_CS,
        .tx_pck_id = TX_ID_DIS1_DATA, .tag = "wayn2.d1",
        .recvbuf = dis1_recvbuf,
    };
    ctx_dis2 = (wayn2_line_ctx_t){
        .spi_host = D2_SPI_HOST, .rclk_pin = D2_RCLK, .out_cs_pin = D2_OUT_CS,
        .tx_pck_id = TX_ID_DIS2_DATA, .tag = "wayn2.d2",
        .recvbuf = dis2_recvbuf,
    };

    // display enable + synthesized CS outputs (D1_OUT_CS/D2_OUT_CS are
    // wired back to D1_IN_CS/D2_IN_CS on this board — see censtar_7cs_digit.c)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = BIT64(DIS_ENB) | BIT64(D1_OUT_CS) | BIT64(D2_OUT_CS);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    // RCLK inputs — falling edge only, used solely for silence timing,
    // never wired directly into the SPI hardware.
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
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
        .name = "w2_wait1", .callback = wait_timer_cb_dis1,
    }, &dis1_wait_timer));
    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "w2_cap1", .callback = capture_timer_cb_dis1,
    }, &dis1_capture_timer));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D1_RCLK, gpio_rclk_negedge_dis1, NULL));

    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "w2_wait2", .callback = wait_timer_cb_dis2,
    }, &dis2_wait_timer));
    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "w2_cap2", .callback = capture_timer_cb_dis2,
    }, &dis2_capture_timer));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D2_RCLK, gpio_rclk_negedge_dis2, NULL));

    ctx_dis1.wait_timer = dis1_wait_timer;
    ctx_dis1.capture_timer = dis1_capture_timer;
    ctx_dis2.wait_timer = dis2_wait_timer;
    ctx_dis2.capture_timer = dis2_capture_timer;

    ptr_send_que = send_queue;

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
    LOG_PRINT("wayn2 starting\r\n");
end:
    return ret;
}
