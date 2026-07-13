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

// One capture batch per channel PER DATA LINE, sent as RAW_CHUNK_COUNT
// packets of RAW_CHUNK_LEN bytes each. SDATA1 and SDATA2 share one
// SCLK/RCLK per channel but carry independent data, so each line is
// captured into its own buffer and sent under its own pck_id
// (TX_ID_RAW_DIS{1,2}_L{1,2}_DATA) — a fully independent stream per
// channel+line, no shared field needed to tell them apart.
//
// Framing model: a codeword is completed every codeword_bits SCLK edges —
// counted purely from SCLK, continuously, regardless of RCLK. RCLK does
// NOT store anything; it only resets the bit-position counter (and the
// in-progress accumulator) back to 0 for both lines. This matters on
// hardware where only one of the two lines' *real* latch signal is wired
// to this board's single RCLK input: that line still gets clean,
// back-to-back codewords purely from SCLK counting, and RCLK — whichever
// line it actually belongs to — just periodically re-syncs the boundary
// rather than being the only thing that produces data.
//
// Only ONE line's batch is sent per "channel full" event — the two lines
// alternate (L1, L2, L1, L2, ...) — followed by RAW_PACING_MS of silence
// before the next capture starts. This keeps each capture-and-send burst
// down to a single 128-byte/4-chunk stream instead of two back-to-back,
// and gives the pacing gap room to be generous (a few seconds) without
// doubling the total time before every line gets a fresh sample.
#define RAW_BATCH_SIZE  128
#define RAW_CHUNK_COUNT 4
#define RAW_CHUNK_LEN   (RAW_BATCH_SIZE / RAW_CHUNK_COUNT)
#define RAW_PACING_MS   10000

// Internal "which channel filled up" marker for capture_queue — distinct
// from the wire pck_id enum, since each channel now maps to two different
// pck_ids (one per data line) rather than one.
#define RAW_CHAN_DIS1 0
#define RAW_CHAN_DIS2 1

#define c_pin_sdata1_read_dis_1 (bool)(gpio_get_level(D1_SDATA1))
#define c_pin_sdata2_read_dis_1 (bool)(gpio_get_level(D1_SDATA2))
#define c_pin_sdata1_read_dis_2 (bool)(gpio_get_level(D2_SDATA1))
#define c_pin_sdata2_read_dis_2 (bool)(gpio_get_level(D2_SDATA2))

static const char *TAG = "raw_cap";

// Configured once at init, read-only afterwards — safe to read from ISR
// context without extra synchronization.
static uint8_t codeword_bits_g = 8;

static QueueHandle_t *ptr_send_que = NULL;
static volatile QueueHandle_t capture_queue = NULL; // signals which channel filled its batch

// SDATA1 and SDATA2 are shifted on the same SCLK edge and flushed on the
// same bit-count boundary, so they always advance in lockstep — one
// shared buffer index and bit counter per channel is enough, covering
// both lines' buffers.
static volatile uint16_t accumulator_dis_1_l1 = 0;
static volatile uint16_t accumulator_dis_1_l2 = 0;
static volatile uint8_t  bit_count_dis_1 = 0; // SCLK edges since the last flush/reset
static volatile uint16_t buf_idx_dis_1 = 0;
static uint8_t raw_buf_dis_1_l1[RAW_BATCH_SIZE];
static uint8_t raw_buf_dis_1_l2[RAW_BATCH_SIZE];

static volatile uint16_t accumulator_dis_2_l1 = 0;
static volatile uint16_t accumulator_dis_2_l2 = 0;
static volatile uint8_t  bit_count_dis_2 = 0; // SCLK edges since the last flush/reset
static volatile uint16_t buf_idx_dis_2 = 0;
static uint8_t raw_buf_dis_2_l1[RAW_BATCH_SIZE];
static uint8_t raw_buf_dis_2_l2[RAW_BATCH_SIZE];

// Which line to send next for each channel — alternates every cycle
// (both lines are always captured together regardless, since both are
// shifted and flushed off the same SCLK; this only controls which one
// gets transmitted).
static uint8_t next_line_dis_1 = 1;
static uint8_t next_line_dis_2 = 1;

static void pin_interrupt_enable_dis_1(void);
static void pin_interrupt_enable_dis_2(void);
static void pin_interrupt_disable_dis_1(void);
static void pin_interrupt_disable_dis_2(void);

// One bit per SCLK edge, shifted into each line's running codeword
// accumulator — both lines sampled on the same edge. Once codeword_bits
// edges have accumulated since the last flush/reset, that's a completed
// codeword: store it for both lines and start counting the next one.
// This is what actually produces data now — RCLK no longer does (see
// word_latch_dis_1/2 below) — so a line still gets clean, back-to-back
// codewords purely from its own SCLK even if this board's one RCLK input
// happens to be wired to the *other* line's real latch signal.
static void IRAM_ATTR bit_read_dis_1(void *arg)
{
    accumulator_dis_1_l1 = (uint16_t)((accumulator_dis_1_l1 << 1) | (uint16_t)c_pin_sdata1_read_dis_1);
    accumulator_dis_1_l2 = (uint16_t)((accumulator_dis_1_l2 << 1) | (uint16_t)c_pin_sdata2_read_dis_1);
    bit_count_dis_1++;

    if (bit_count_dis_1 >= codeword_bits_g)
    {
        const uint8_t bytes = (codeword_bits_g <= 8) ? 1 : 2;
        if (buf_idx_dis_1 + bytes <= RAW_BATCH_SIZE)
        {
            raw_buf_dis_1_l1[buf_idx_dis_1] = (uint8_t)accumulator_dis_1_l1;
            raw_buf_dis_1_l2[buf_idx_dis_1] = (uint8_t)accumulator_dis_1_l2;
            if (bytes == 2)
            {
                raw_buf_dis_1_l1[buf_idx_dis_1 + 1] = (uint8_t)(accumulator_dis_1_l1 >> 8);
                raw_buf_dis_1_l2[buf_idx_dis_1 + 1] = (uint8_t)(accumulator_dis_1_l2 >> 8);
            }
            buf_idx_dis_1 = (uint16_t)(buf_idx_dis_1 + bytes);
        }
        accumulator_dis_1_l1 = 0;
        accumulator_dis_1_l2 = 0;
        bit_count_dis_1 = 0;

        if (buf_idx_dis_1 >= RAW_BATCH_SIZE)
        {
            pin_interrupt_disable_dis_1();
            const uint8_t chan = RAW_CHAN_DIS1;
            xQueueSendFromISR(capture_queue, &chan, NULL);
        }
    }
}
static void IRAM_ATTR bit_read_dis_2(void *arg)
{
    accumulator_dis_2_l1 = (uint16_t)((accumulator_dis_2_l1 << 1) | (uint16_t)c_pin_sdata1_read_dis_2);
    accumulator_dis_2_l2 = (uint16_t)((accumulator_dis_2_l2 << 1) | (uint16_t)c_pin_sdata2_read_dis_2);
    bit_count_dis_2++;

    if (bit_count_dis_2 >= codeword_bits_g)
    {
        const uint8_t bytes = (codeword_bits_g <= 8) ? 1 : 2;
        if (buf_idx_dis_2 + bytes <= RAW_BATCH_SIZE)
        {
            raw_buf_dis_2_l1[buf_idx_dis_2] = (uint8_t)accumulator_dis_2_l1;
            raw_buf_dis_2_l2[buf_idx_dis_2] = (uint8_t)accumulator_dis_2_l2;
            if (bytes == 2)
            {
                raw_buf_dis_2_l1[buf_idx_dis_2 + 1] = (uint8_t)(accumulator_dis_2_l1 >> 8);
                raw_buf_dis_2_l2[buf_idx_dis_2 + 1] = (uint8_t)(accumulator_dis_2_l2 >> 8);
            }
            buf_idx_dis_2 = (uint16_t)(buf_idx_dis_2 + bytes);
        }
        accumulator_dis_2_l1 = 0;
        accumulator_dis_2_l2 = 0;
        bit_count_dis_2 = 0;

        if (buf_idx_dis_2 >= RAW_BATCH_SIZE)
        {
            pin_interrupt_disable_dis_2();
            const uint8_t chan = RAW_CHAN_DIS2;
            xQueueSendFromISR(capture_queue, &chan, NULL);
        }
    }
}

// RCLK edge = re-sync only. It does NOT store a codeword — it just resets
// the bit-position counter (and discards whatever partial bits had
// accumulated since the last flush) for both lines, so the next
// codeword_bits-wide group starts counting fresh from here. Framing is
// driven entirely by bit_read_dis_1/2 above.
static void IRAM_ATTR word_latch_dis_1(void *arg)
{
    accumulator_dis_1_l1 = 0;
    accumulator_dis_1_l2 = 0;
    bit_count_dis_1 = 0;
}
static void IRAM_ATTR word_latch_dis_2(void *arg)
{
    accumulator_dis_2_l1 = 0;
    accumulator_dis_2_l2 = 0;
    bit_count_dis_2 = 0;
}

static void send_one_line_batch(tx_pckt_id_t pck_id, const uint8_t *buf)
{
    const display_type_t dtype = (codeword_bits_g <= 8) ? DIS_RAW_8BIT_V1 : DIS_RAW_12BIT_V1;

    for (uint8_t idx = 0; idx < RAW_CHUNK_COUNT; idx++)
    {
        data_packet_t pkt = {
            .pck_id = (uint8_t)pck_id,
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

// Sends only ONE data line's batch for the channel that just filled up —
// whichever line's turn it is — then flips to the other line for next
// time. Both lines were captured either way; the unsent one's fresh data
// just waits for its own turn.
static void send_raw_batch(uint8_t chan)
{
    if (chan == RAW_CHAN_DIS1)
    {
        if (next_line_dis_1 == 1)
        {
            send_one_line_batch(TX_ID_RAW_DIS1_L1_DATA, raw_buf_dis_1_l1);
            next_line_dis_1 = 2;
        }
        else
        {
            send_one_line_batch(TX_ID_RAW_DIS1_L2_DATA, raw_buf_dis_1_l2);
            next_line_dis_1 = 1;
        }
    }
    else
    {
        if (next_line_dis_2 == 1)
        {
            send_one_line_batch(TX_ID_RAW_DIS2_L1_DATA, raw_buf_dis_2_l1);
            next_line_dis_2 = 2;
        }
        else
        {
            send_one_line_batch(TX_ID_RAW_DIS2_L2_DATA, raw_buf_dis_2_l2);
            next_line_dis_2 = 1;
        }
    }
}

static void data_send_task(void *arg)
{
    uint8_t chan;
    while (1)
    {
        if (xQueueReceive(capture_queue, &chan, pdMS_TO_TICKS(10)))
        {
            send_raw_batch(chan);
            vTaskDelay(pdMS_TO_TICKS(RAW_PACING_MS)); // pacing delay so main/cloud can keep up

            if (chan == RAW_CHAN_DIS1)
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
    accumulator_dis_1_l1 = 0;
    accumulator_dis_1_l2 = 0;
    bit_count_dis_1 = 0;
    gpio_set_intr_type(D1_SCLK, GPIO_INTR_POSEDGE);
    gpio_set_intr_type(D1_RCLK, GPIO_INTR_POSEDGE);
}
static void pin_interrupt_enable_dis_2(void)
{
    accumulator_dis_2_l1 = 0;
    accumulator_dis_2_l2 = 0;
    bit_count_dis_2 = 0;
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

    // SDATA1 + SDATA2 — both lines are actively captured by this driver
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(D1_SDATA1) | BIT64(D1_SDATA2) | BIT64(D2_SDATA1) | BIT64(D2_SDATA2);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
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
