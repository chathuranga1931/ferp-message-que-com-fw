#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sys_types.h"
#include "board_io.h"
#include "device.h"
#include "esp_log.h"
#include "raw_capture.h"

// One capture batch per channel, sent as RAW_CHUNK_COUNT packets of
// RAW_CHUNK_LEN bytes each, then a pacing delay before the next batch —
// see PLAN.md for why (downstream log forwarding can't keep up with a
// continuous stream).
#define RAW_BATCH_SIZE  256
#define RAW_CHUNK_COUNT 4
#define RAW_CHUNK_LEN   (RAW_BATCH_SIZE / RAW_CHUNK_COUNT)
#define RAW_PACING_MS   2000

#define c_pin_sdata1_read_dis_1 (bool)(gpio_get_level(D1_SDATA1))
#define c_pin_sdata1_read_dis_2 (bool)(gpio_get_level(D2_SDATA1))

static const char *TAG = "raw_cap";

// Configured once at init, read-only afterwards — safe to read from ISR
// context without extra synchronization.
static uint8_t codeword_bits_g = 8;

static QueueHandle_t *ptr_send_que = NULL;
static volatile QueueHandle_t capture_queue = NULL; // signals which channel filled its batch

static volatile uint16_t accumulator_dis_1 = 0;
static volatile uint16_t buf_idx_dis_1 = 0;
static uint8_t raw_buf_dis_1[RAW_BATCH_SIZE];

static volatile uint16_t accumulator_dis_2 = 0;
static volatile uint16_t buf_idx_dis_2 = 0;
static uint8_t raw_buf_dis_2[RAW_BATCH_SIZE];

static void pin_interrupt_enable_dis_1(void);
static void pin_interrupt_enable_dis_2(void);
static void pin_interrupt_disable_dis_1(void);
static void pin_interrupt_disable_dis_2(void);

// One bit per SCLK edge, shifted into the running codeword accumulator.
static void IRAM_ATTR bit_read_dis_1(void *arg)
{
    accumulator_dis_1 = (uint16_t)((accumulator_dis_1 << 1) | (uint16_t)c_pin_sdata1_read_dis_1);
}
static void IRAM_ATTR bit_read_dis_2(void *arg)
{
    accumulator_dis_2 = (uint16_t)((accumulator_dis_2 << 1) | (uint16_t)c_pin_sdata1_read_dis_2);
}

// RCLK edge = one completed codeword (standard shift-register latch
// behaviour: SCLK shifts bits in, RCLK/STCP copies the shift register to
// the output latch). Whatever accumulated since the last RCLK is stored
// as-is — no bit-count filtering, since capturing exactly what the pump
// sends (even if it doesn't match codeword_bits) is the point of this
// driver.
static void IRAM_ATTR word_latch_dis_1(void *arg)
{
    const uint8_t bytes = (codeword_bits_g <= 8) ? 1 : 2;
    if (buf_idx_dis_1 + bytes <= RAW_BATCH_SIZE)
    {
        raw_buf_dis_1[buf_idx_dis_1] = (uint8_t)accumulator_dis_1;
        if (bytes == 2)
            raw_buf_dis_1[buf_idx_dis_1 + 1] = (uint8_t)(accumulator_dis_1 >> 8);
        buf_idx_dis_1 = (uint16_t)(buf_idx_dis_1 + bytes);
    }
    accumulator_dis_1 = 0;

    if (buf_idx_dis_1 >= RAW_BATCH_SIZE)
    {
        pin_interrupt_disable_dis_1();
        const uint8_t chan = TX_ID_RAW_DIS1_DATA;
        xQueueSendFromISR(capture_queue, &chan, NULL);
    }
}
static void IRAM_ATTR word_latch_dis_2(void *arg)
{
    const uint8_t bytes = (codeword_bits_g <= 8) ? 1 : 2;
    if (buf_idx_dis_2 + bytes <= RAW_BATCH_SIZE)
    {
        raw_buf_dis_2[buf_idx_dis_2] = (uint8_t)accumulator_dis_2;
        if (bytes == 2)
            raw_buf_dis_2[buf_idx_dis_2 + 1] = (uint8_t)(accumulator_dis_2 >> 8);
        buf_idx_dis_2 = (uint16_t)(buf_idx_dis_2 + bytes);
    }
    accumulator_dis_2 = 0;

    if (buf_idx_dis_2 >= RAW_BATCH_SIZE)
    {
        pin_interrupt_disable_dis_2();
        const uint8_t chan = TX_ID_RAW_DIS2_DATA;
        xQueueSendFromISR(capture_queue, &chan, NULL);
    }
}

static void send_raw_batch(uint8_t chan_id)
{
    const uint8_t *buf = (chan_id == TX_ID_RAW_DIS1_DATA) ? raw_buf_dis_1 : raw_buf_dis_2;
    const display_type_t dtype = (codeword_bits_g <= 8) ? DIS_RAW_8BIT_V1 : DIS_RAW_12BIT_V1;

    for (uint8_t idx = 0; idx < RAW_CHUNK_COUNT; idx++)
    {
        data_packet_t pkt = {
            .pck_id = chan_id,
            .display = (uint8_t)dtype,
            .length = sizeof(raw_capture_chunk_t) + RAW_CHUNK_LEN};
        raw_capture_chunk_t *chunk = (raw_capture_chunk_t *)pkt.ab_data;
        chunk->codeword_bits = codeword_bits_g;
        chunk->total_len = RAW_BATCH_SIZE;
        chunk->chunk_index = idx;
        chunk->chunk_count = RAW_CHUNK_COUNT;
        chunk->chunk_len = RAW_CHUNK_LEN;
        memcpy(chunk->data, buf + (idx * RAW_CHUNK_LEN), RAW_CHUNK_LEN);

        xQueueSend(*ptr_send_que, (void *)&pkt, pdMS_TO_TICKS(10)); // if queue is full, wait until it gets clear
        vTaskDelay(pdMS_TO_TICKS(10));                              // inter-packet gap, matches serial_send_task's own gap
    }
}

static void data_send_task(void *arg)
{
    uint8_t chan_id;
    while (1)
    {
        if (xQueueReceive(capture_queue, &chan_id, pdMS_TO_TICKS(10)))
        {
            send_raw_batch(chan_id);
            vTaskDelay(pdMS_TO_TICKS(RAW_PACING_MS)); // pacing delay so main/cloud can keep up

            if (chan_id == TX_ID_RAW_DIS1_DATA)
            {
                buf_idx_dis_1 = 0;
                pin_interrupt_enable_dis_1();
            }
            else
            {
                buf_idx_dis_2 = 0;
                pin_interrupt_enable_dis_2();
            }
        }
    }
    vTaskDelete(NULL);
}

static void pin_interrupt_enable_dis_1(void)
{
    accumulator_dis_1 = 0;
    gpio_set_intr_type(D1_SCLK, GPIO_INTR_POSEDGE);
    gpio_set_intr_type(D1_RCLK, GPIO_INTR_POSEDGE);
}
static void pin_interrupt_enable_dis_2(void)
{
    accumulator_dis_2 = 0;
    gpio_set_intr_type(D2_SCLK, GPIO_INTR_POSEDGE);
    gpio_set_intr_type(D2_RCLK, GPIO_INTR_POSEDGE);
}
static void pin_interrupt_disable_dis_1(void)
{
    gpio_set_intr_type(D1_SCLK, GPIO_INTR_DISABLE);
    gpio_set_intr_type(D1_RCLK, GPIO_INTR_DISABLE);
}
static void pin_interrupt_disable_dis_2(void)
{
    gpio_set_intr_type(D2_SCLK, GPIO_INTR_DISABLE);
    gpio_set_intr_type(D2_RCLK, GPIO_INTR_DISABLE);
}

esp_err_t display_raw_capture_init(QueueHandle_t *send_queue, uint8_t codeword_bits)
{
    esp_err_t ret = ESP_OK;
    gpio_config_t io_conf;

    codeword_bits_g = codeword_bits;

    // set display enable pin output and turn off, set chip select signals out
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = BIT64(DIS_ENB) | BIT64(D1_OUT_CS) | BIT64(D2_OUT_CS);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    // clock/latch inputs, interrupt enabled once capture starts
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(D1_SCLK) | BIT64(D1_RCLK) | BIT64(D2_SCLK) | BIT64(D2_RCLK);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    // SDATA1 — the line this driver actually captures
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(D1_SDATA1) | BIT64(D2_SDATA1);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    // SDATA2 — not captured by this driver, pulled down like other drivers
    // do for the line they don't use
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(D1_SDATA2) | BIT64(D2_SDATA2);
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    // chip select inputs
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(D1_IN_CS) | BIT64(D2_IN_CS);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_GOTO(ret, end, gpio_config(&io_conf));

    ESP_ERROR_GOTO(ret, end, gpio_install_isr_service(0));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D1_SCLK, bit_read_dis_1, NULL));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D1_RCLK, word_latch_dis_1, NULL));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D2_SCLK, bit_read_dis_2, NULL));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D2_RCLK, word_latch_dis_2, NULL));

    capture_queue = xQueueCreate(4, sizeof(uint8_t));
    if (capture_queue == NULL)
    {
        ret = ESP_FAIL;
        goto end;
    }

    ptr_send_que = send_queue;

    if (xTaskCreate(data_send_task, "raw_capture_send_task", 2 * 1024, NULL, 5, NULL) != pdPASS)
    {
        ret = ESP_FAIL;
        goto end;
    }

    pin_interrupt_enable_dis_1();
    pin_interrupt_enable_dis_2();
    gpio_set_level(DIS_ENB, true);
    ESP_LOGI(TAG, "Starting raw capture, codeword_bits=%d\r\n", codeword_bits_g);
end:
    return ret;
}
