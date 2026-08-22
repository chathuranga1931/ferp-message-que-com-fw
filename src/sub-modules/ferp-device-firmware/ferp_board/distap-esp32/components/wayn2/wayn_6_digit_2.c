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
// slave — modeled directly on censtar_7cs_digit.c's proven pattern: the
// pump has no real CS line, so an RCLK-silence timer synthesizes one via
// the board's D1_OUT_CS -> D1_IN_CS loopback.
//
// Only the leading 15-byte block (2-byte 0xFF 0xFF sync + 13-byte payload:
// Volume[0:6] + Total[6:12] + 1 unknown byte) is decoded. The remaining
// ~80 bytes of each 95-byte cycle (two addressed groups believed to carry
// unit price / rate) are captured but not decoded yet — deferred until a
// rate-varying capture is available to reverse-engineer them.

#define D1_SPI_HOST SPI2_HOST
#define D2_SPI_HOST SPI3_HOST

#define RECVBUF_SIZE      128 // comfortably above one full 95-byte cycle
#define DIGIT_GROUP_LEN   6
#define MIN_CAPTURE_BYTES 15  // 2 sync + 13 payload bytes we need

// Measured on Wayne Type02 ground-truth captures: intra-frame gaps stay
// under ~1ms, inter-frame (cycle boundary) gaps are 50-109ms — 10ms sits
// with ~10x margin on both sides.
#define TIMER_TOUT_US         (10 * 1000)  // RCLK-silence -> synthesize CS edge
#define TIMER_TOUT_CAPTURE_US (150 * 1000) // overall capture watchdog

static QueueHandle_t *ptr_send_que = NULL;

WORD_ALIGNED_ATTR static uint8_t dis1_recvbuf[RECVBUF_SIZE] = {};
static esp_timer_handle_t dis1_timer = NULL, dis1_timer_cs = NULL;
static volatile bool dis1_cs_level, dis1_cs_tout;

WORD_ALIGNED_ATTR static uint8_t dis2_recvbuf[RECVBUF_SIZE] = {};
static esp_timer_handle_t dis2_timer = NULL, dis2_timer_cs = NULL;
static volatile bool dis2_cs_level, dis2_cs_tout;

static const char *TAG = "wayn2";

static void task_spi_data(void *arg);

IRAM_ATTR static void timer_cs_tout_dis1(void *arg)
{
    gpio_set_level(D1_OUT_CS, true);
    dis1_cs_tout = true;
}
IRAM_ATTR static void timer_rclk_tout_dis1(void *arg)
{
    gpio_set_level(D1_OUT_CS, true);
    dis1_cs_level = true;
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
    dis2_cs_tout = true;
}
IRAM_ATTR static void timer_rclk_tout_dis2(void *arg)
{
    gpio_set_level(D2_OUT_CS, true);
    dis2_cs_level = true;
}
IRAM_ATTR static void gpio_rclk_done_dis2(void *arg)
{
    if (esp_timer_is_active(dis2_timer))
        esp_timer_restart(dis2_timer, TIMER_TOUT_US);
    else
        esp_timer_start_once(dis2_timer, TIMER_TOUT_US);
}

// Maps a raw segment byte (decimal-point bit masked off) to its digit 0-9.
// 0x00 is a blanked/unused leading-zero position, valued as digit 0.
// Digits 4/5/6 are carried over from the old wayn_6_digit.c table —
// UNVERIFIED against real Type02 captures (never appeared in either
// ground-truth dataset). Digit 9 (0xE6) differs from the old table's 0xF6
// and IS confirmed for this display (bottom segment off).
static bool segment_to_digit(uint8_t raw, uint8_t *digit)
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

// Decodes one LSB-first 6-byte digit group into a raw natural-scale value
// (the plain 6-digit integer the display shows, decimal point stripped —
// e.g. Volume "000.127" -> 127, Total "039.8" -> 398). Deliberately NOT
// rescaled to the canonical x100/x1000 convention here — per the existing
// pattern in this codebase (see DIS_CENSTAR_6_DIGIT), that correction
// belongs in fuel_types_from_frame() on the main-esp32 side.
static bool decode_digit_group(const uint8_t *group, uint32_t *value)
{
    uint32_t v = 0;
    uint32_t weight = 1;
    for (uint8_t i = 0; i < DIGIT_GROUP_LEN; i++)
    {
        uint8_t digit;
        if (!segment_to_digit(group[i], &digit))
            return false;
        v += (uint32_t)digit * weight;
        weight *= 10;
    }
    *value = v;
    return true;
}

static bool decode_main_block(const uint8_t *buf, display_data_t *dd)
{
    if (buf[0] != 0xFF || buf[1] != 0xFF)
        return false;

    uint32_t volume, total;
    if (!decode_digit_group(&buf[2], &volume))
        return false;
    if (!decode_digit_group(&buf[8], &total))
        return false;

    memset(dd, 0, sizeof(*dd));
    dd->volume_l    = volume;
    dd->total_price = total;
    dd->unit_price  = 0; // not decoded yet — see fuel_types_from_frame()
    return true;
}

static void task_spi_data(void *arg)
{
    esp_err_t ret = ESP_OK;
    TickType_t ticks_now;
    display_data_t capture_now = {};
    data_packet_t display_data = {
        .display = DIS_WAYNE_6_DIGIT_2,
        .length = sizeof(display_data_t)};

    spi_slave_transaction_t spi_data_dis1 = {
        .length = sizeof(dis1_recvbuf) * 8,
        .rx_buffer = dis1_recvbuf,
    };
    display_data_t cap_data_dis1 = {};
    TickType_t ticks_last_dis1 = xTaskGetTickCount();
    memset(dis1_recvbuf, 0, sizeof(dis1_recvbuf));
    gpio_set_level(D1_OUT_CS, false); // start capturing
    dis1_cs_level = false;

    spi_slave_transaction_t spi_data_dis2 = {
        .length = sizeof(dis2_recvbuf) * 8,
        .rx_buffer = dis2_recvbuf,
    };
    display_data_t cap_data_dis2 = {};
    TickType_t ticks_last_dis2 = xTaskGetTickCount();
    memset(dis2_recvbuf, 0, sizeof(dis2_recvbuf));
    gpio_set_level(D2_OUT_CS, false); // start capturing
    dis2_cs_level = false;

    while (1)
    {
        /* Capture data from display 1 */
        if (dis1_cs_level)
        {
            dis1_cs_tout = false;
            memset(dis1_recvbuf, 0, sizeof(dis1_recvbuf));
            spi_data_dis1.trans_len = 0;
            esp_timer_stop(dis1_timer);
            gpio_set_level(D1_OUT_CS, false); // reassert CS, arm next capture
            esp_timer_start_once(dis1_timer_cs, TIMER_TOUT_CAPTURE_US);
            ret = spi_slave_transmit(D1_SPI_HOST, &spi_data_dis1, pdMS_TO_TICKS(2 * 1000));
            esp_timer_stop(dis1_timer_cs);
            esp_timer_stop(dis1_timer);
            if (dis1_cs_tout || ret != ESP_OK || (spi_data_dis1.trans_len / 8) < MIN_CAPTURE_BYTES)
            {
                ESP_LOGI(TAG, "dis1 cstout:%d, fail:0x%.2x len:%d", dis1_cs_tout, ret, spi_data_dis1.trans_len / 8);
                goto end_dis1;
            }

            if (!decode_main_block(dis1_recvbuf, &capture_now))
            {
                ESP_LOGE(TAG, "dis1 decode error");
                goto end_dis1;
            }

            ticks_now = xTaskGetTickCount();
            if (ticks_now - ticks_last_dis1 > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) || memcmp(&cap_data_dis1, &capture_now, sizeof(display_data_t)))
            {
                ticks_last_dis1 = ticks_now;
                display_data.pck_id = TX_ID_DIS1_DATA;
                memcpy(display_data.ab_data, &capture_now, sizeof(display_data_t));
                memcpy(&cap_data_dis1, &capture_now, sizeof(display_data_t));
                xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
            }
        end_dis1:
            dis1_cs_level = false;
        }

        /* Capture data from display 2 */
        if (dis2_cs_level)
        {
            dis2_cs_tout = false;
            memset(dis2_recvbuf, 0, sizeof(dis2_recvbuf));
            spi_data_dis2.trans_len = 0;
            esp_timer_stop(dis2_timer);
            gpio_set_level(D2_OUT_CS, false); // reassert CS, arm next capture
            esp_timer_start_once(dis2_timer_cs, TIMER_TOUT_CAPTURE_US);
            ret = spi_slave_transmit(D2_SPI_HOST, &spi_data_dis2, pdMS_TO_TICKS(2 * 1000));
            esp_timer_stop(dis2_timer_cs);
            esp_timer_stop(dis2_timer);
            if (dis2_cs_tout || ret != ESP_OK || (spi_data_dis2.trans_len / 8) < MIN_CAPTURE_BYTES)
            {
                ESP_LOGI(TAG, "dis2 cstout:%d, fail:0x%.2x len:%d", dis2_cs_tout, ret, spi_data_dis2.trans_len / 8);
                goto end_dis2;
            }

            if (!decode_main_block(dis2_recvbuf, &capture_now))
            {
                ESP_LOGE(TAG, "dis2 decode error");
                goto end_dis2;
            }

            ticks_now = xTaskGetTickCount();
            if (ticks_now - ticks_last_dis2 > pdMS_TO_TICKS(DIFF_PCKT_SEND_MS) || memcmp(&cap_data_dis2, &capture_now, sizeof(display_data_t)))
            {
                ticks_last_dis2 = ticks_now;
                display_data.pck_id = TX_ID_DIS2_DATA;
                memcpy(display_data.ab_data, &capture_now, sizeof(display_data_t));
                memcpy(&cap_data_dis2, &capture_now, sizeof(display_data_t));
                xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
            }
        end_dis2:
            dis2_cs_level = false;
        }

        /* Periodic resend of last-known-good value even if unchanged */
        ticks_now = xTaskGetTickCount();
        if ((ticks_now - ticks_last_dis1) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
        {
            ticks_last_dis1 = ticks_now;
            display_data.pck_id = TX_ID_DIS1_DATA;
            memcpy(display_data.ab_data, &cap_data_dis1, sizeof(display_data_t));
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
        if ((ticks_now - ticks_last_dis2) > pdMS_TO_TICKS(SAME_PCKT_SEND_MS))
        {
            ticks_last_dis2 = ticks_now;
            display_data.pck_id = TX_ID_DIS2_DATA;
            memcpy(display_data.ab_data, &cap_data_dis2, sizeof(display_data_t));
            xQueueSend(*ptr_send_que, (void *)&display_data, pdMS_TO_TICKS(10));
        }
        vTaskDelay(1);
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

    // RCLK inputs — posedge interrupt used only for silence timing, never
    // wired directly into the SPI hardware
    io_conf.intr_type = GPIO_INTR_POSEDGE;
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

    if (xTaskCreate(task_spi_data, "task_wayn2_spi", 8 * 1024, NULL, 8, NULL) == pdFALSE)
    {
        ret = ESP_FAIL;
        goto end;
    }

    gpio_set_level(DIS_ENB, true);
    ESP_LOGI(TAG, "Starting Wayne Type02 display capture\r\n");
end:
    return ret;
}
