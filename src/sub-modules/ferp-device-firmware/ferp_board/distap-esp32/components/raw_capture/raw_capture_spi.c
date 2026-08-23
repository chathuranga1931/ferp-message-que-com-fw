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
#include "raw_capture_spi.h"

// SPI-slave + synthesized-CS raw capture — diagnostic tool (display type
// DIS_RAW_SPI_V1 = 92) to verify the capture mechanism shared with
// DIS_WAYNE_6_DIGIT_2 independently of any digit-decoding logic. No
// decoding here — the raw buffer is forwarded to main as-is via the same
// chunked raw_capture_chunk_t protocol DIS_RAW_8BIT_V1/12BIT_V1 use.
//
// Per-line sequence (as specified — deliberately NOT the same timing as
// wayne2's full-cycle capture, to isolate whether this timing scheme
// captures the bus more reliably):
//   1. RCLK falling edge restarts a ~45ms "wait" timer.
//   2. Once 45ms passes with no further falling edge, the RCLK interrupt
//      is disabled (no more interference once capturing has started),
//      CS is driven low (arm), and a separate, fixed ~15ms "capture
//      window" timer starts — NOT reset by any further RCLK activity.
//   3. When the capture-window timer fires, CS is driven high — this
//      ends the pending SPI-slave transaction (transaction completion on
//      classic ESP32 is tied to CS, not to how many bytes were clocked
//      in) regardless of where the pump is in its cycle.
//   4. The captured buffer (whatever arrived in that ~15ms window) is
//      sent as-is, the RCLK interrupt is re-enabled, and the line goes
//      back to waiting for the next falling edge.
//
// Because CS asserts a fixed ~45ms after the bus goes quiet rather than
// being bound to the pump's own end-of-cycle signal, and the real
// inter-cycle gap varies (~50-109ms), there's a known, accepted risk of
// the capture window starting a byte or two into the frame rather than
// exactly on the sync bytes — this tool exists to see exactly how far off
// (if at all) in practice.
//
// Only SDATA1 per channel — an SPI slave peripheral has one MOSI input;
// unlike GPIO bit-bang it cannot also capture SDATA2 independently.

#define D1_SPI_HOST SPI2_HOST
#define D2_SPI_HOST SPI3_HOST

#define RECVBUF_SIZE   128 // generous margin over what ~15ms of bus activity can produce
#define RAW_CHUNK_COUNT 4
#define RAW_CHUNK_LEN   (RECVBUF_SIZE / RAW_CHUNK_COUNT)
#define RAW_PACING_MS   2000 // matches raw_capture.c's own inter-batch pacing

#define WAIT_TIMER_US    (45 * 1000) // RCLK-silence -> safe to arm
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
} raw_spi_line_ctx_t;

WORD_ALIGNED_ATTR static uint8_t dis1_recvbuf[RECVBUF_SIZE] = {};
WORD_ALIGNED_ATTR static uint8_t dis2_recvbuf[RECVBUF_SIZE] = {};

static esp_timer_handle_t dis1_wait_timer = NULL, dis1_capture_timer = NULL;
static esp_timer_handle_t dis2_wait_timer = NULL, dis2_capture_timer = NULL;

static raw_spi_line_ctx_t ctx_dis1;
static raw_spi_line_ctx_t ctx_dis2;

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
    gpio_set_level(D2_OUT_CS, false);
}
IRAM_ATTR static void gpio_rclk_negedge_dis2(void *arg)
{
    if (esp_timer_is_active(dis2_wait_timer))
        esp_timer_restart(dis2_wait_timer, WAIT_TIMER_US);
    else
        esp_timer_start_once(dis2_wait_timer, WAIT_TIMER_US);
}

// Sends the full RECVBUF_SIZE buffer as RAW_CHUNK_COUNT chunks — same wire
// format as raw_capture.c's send_one_line_batch(), so the existing
// main-esp32 receiver (_raw_hex_dump) displays it with no changes needed.
static void send_raw_batch(raw_spi_line_ctx_t *c)
{
    for (uint8_t idx = 0; idx < RAW_CHUNK_COUNT; idx++)
    {
        data_packet_t pkt = {
            .pck_id = (uint8_t)c->tx_pck_id,
            .display = (uint8_t)DIS_RAW_SPI_V1,
            .length = sizeof(raw_capture_chunk_t) + RAW_CHUNK_LEN};
        raw_capture_chunk_t *chunk = (raw_capture_chunk_t *)pkt.ab_data;
        chunk->codeword_bits = 8;
        chunk->total_len = RECVBUF_SIZE;
        chunk->chunk_index = idx;
        chunk->chunk_count = RAW_CHUNK_COUNT;
        chunk->chunk_len = RAW_CHUNK_LEN;
        memcpy(chunk->data, c->recvbuf + (idx * RAW_CHUNK_LEN), RAW_CHUNK_LEN);

        xQueueSend(*ptr_send_que, (void *)&pkt, pdMS_TO_TICKS(10));
        vTaskDelay(pdMS_TO_TICKS(10)); // inter-packet gap, matches raw_capture.c's own pacing
    }
}

static void task_line_capture(void *arg)
{
    raw_spi_line_ctx_t *c = (raw_spi_line_ctx_t *)arg;
    c->task = xTaskGetCurrentTaskHandle();

    spi_slave_transaction_t trans = {
        .length = RECVBUF_SIZE * 8,
        .rx_buffer = c->recvbuf,
    };

    gpio_set_level(c->out_cs_pin, true); // idle

    while (1)
    {
        // RCLK interrupt is OFF from here until just before this point next
        // time around — critically, that includes the entire RAW_PACING_MS
        // sleep below. Re-enabling it right after a capture (instead of
        // here) let the wait_timer fire and notify while the task was still
        // asleep for the full pacing delay; by the time the task woke up
        // and acted on that stale notification, ~RAW_PACING_MS had passed
        // since the timer actually fired — arming CS at a essentially
        // random point relative to the bus instead of a real cycle
        // boundary, which (given the gap is a small fraction of each full
        // cycle) landed on silence almost every time.
        (void)ulTaskNotifyTake(pdTRUE, 0); // discard any stale count before re-enabling
        gpio_intr_enable(c->rclk_pin);

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // blocks until wait_timer fires (no polling jitter)

        // Pre-fill with a sentinel that can't occur on the real bus (not
        // 0x00 — a genuinely idle/quiet line reads as 0x00 too, which would
        // be indistinguishable from "SPI captured nothing at all"). Any
        // 0xEE left in the sent buffer means the SPI peripheral never
        // clocked in a bit there — a real, visible signal that the
        // capture window missed the bus activity entirely.
        memset(c->recvbuf, 0xEE, RECVBUF_SIZE);
        trans.trans_len = 0;

        gpio_set_level(c->out_cs_pin, false); // CS low — arm
        esp_timer_start_once(c->capture_timer, CAPTURE_WINDOW_US);

        esp_err_t ret = spi_slave_transmit(c->spi_host, &trans, pdMS_TO_TICKS(1000));

        esp_timer_stop(c->capture_timer);
        gpio_set_level(c->out_cs_pin, true); // ensure high (capture_timer should already have done this)

        size_t got_bytes = trans.trans_len / 8;
        ESP_LOGI(c->tag, "captured %u bytes (ret=0x%.2x)", (unsigned)got_bytes, ret);
        LOG_PRINT("%s captured %u bytes (ret=0x%.2x)\r\n", c->tag, (unsigned)got_bytes, ret);

        send_raw_batch(c);
        vTaskDelay(pdMS_TO_TICKS(RAW_PACING_MS)); // pacing so main/cloud can keep up — RCLK stays disabled throughout
    }
}

esp_err_t display_raw_capture_spi_init(QueueHandle_t *send_queue)
{
    esp_err_t ret = ESP_OK;
    gpio_config_t io_conf;

    // Populate contexts BEFORE installing the RCLK ISR / creating timers,
    // so a stray RCLK edge during setup can never touch an unpopulated ctx.
    ctx_dis1 = (raw_spi_line_ctx_t){
        .spi_host = D1_SPI_HOST, .rclk_pin = D1_RCLK, .out_cs_pin = D1_OUT_CS,
        .tx_pck_id = TX_ID_RAW_DIS1_L1_DATA, .tag = "rawspi.d1",
        .recvbuf = dis1_recvbuf,
    };
    ctx_dis2 = (raw_spi_line_ctx_t){
        .spi_host = D2_SPI_HOST, .rclk_pin = D2_RCLK, .out_cs_pin = D2_OUT_CS,
        .tx_pck_id = TX_ID_RAW_DIS2_L1_DATA, .tag = "rawspi.d2",
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

    // RCLK inputs — falling edge only, per spec
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
        .name = "rs_wait1", .callback = wait_timer_cb_dis1,
    }, &dis1_wait_timer));
    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "rs_cap1", .callback = capture_timer_cb_dis1,
    }, &dis1_capture_timer));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D1_RCLK, gpio_rclk_negedge_dis1, NULL));

    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "rs_wait2", .callback = wait_timer_cb_dis2,
    }, &dis2_wait_timer));
    ESP_ERROR_GOTO(ret, end, esp_timer_create(&(const esp_timer_create_args_t){
        .name = "rs_cap2", .callback = capture_timer_cb_dis2,
    }, &dis2_capture_timer));
    ESP_ERROR_GOTO(ret, end, gpio_isr_handler_add(D2_RCLK, gpio_rclk_negedge_dis2, NULL));

    ctx_dis1.wait_timer = dis1_wait_timer;
    ctx_dis1.capture_timer = dis1_capture_timer;
    ctx_dis2.wait_timer = dis2_wait_timer;
    ctx_dis2.capture_timer = dis2_capture_timer;

    ptr_send_que = send_queue;

    if (xTaskCreate(task_line_capture, "task_rawspi_d1", 8 * 1024, &ctx_dis1, 8, NULL) == pdFALSE)
    {
        ret = ESP_FAIL;
        goto end;
    }
    if (xTaskCreate(task_line_capture, "task_rawspi_d2", 8 * 1024, &ctx_dis2, 8, NULL) == pdFALSE)
    {
        ret = ESP_FAIL;
        goto end;
    }

    gpio_set_level(DIS_ENB, true);
    ESP_LOGI("rawspi", "Starting SPI raw capture (type 92)\r\n");
    LOG_PRINT("rawspi starting (type 92)\r\n");
end:
    return ret;
}
