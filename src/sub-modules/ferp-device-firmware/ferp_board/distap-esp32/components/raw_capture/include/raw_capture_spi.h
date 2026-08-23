#ifndef _RAW_CAPTURE_SPI_H_
#define _RAW_CAPTURE_SPI_H_

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Initialise the SPI-slave raw-capture driver (display type
 * DIS_RAW_SPI_V1 = 92) — a diagnostic tool to verify the SPI + synthesized
 * CS capture mechanism (shared with DIS_WAYNE_6_DIGIT_2) independently of
 * any digit-decoding logic. No decoding is performed here: the raw
 * captured buffer is forwarded to main as-is via the same chunked
 * raw_capture_chunk_t protocol used by DIS_RAW_8BIT_V1/12BIT_V1
 * (TX_ID_RAW_DIS1_L1_DATA / TX_ID_RAW_DIS2_L1_DATA — SDATA1 only per
 * channel; SPI slave hardware can't independently receive a second data
 * line the way GPIO bit-bang can).
 *
 * Per-line capture sequence:
 *   1. Wait for an RCLK falling edge; each one restarts a ~45ms timer.
 *   2. Once that timer fires (45ms of confirmed silence), disable the
 *      RCLK interrupt, drive CS low (arm), and start a second, fixed
 *      ~15ms timer.
 *   3. When that timer fires, drive CS high — this ends the pending
 *      SPI-slave transaction regardless of further bus activity.
 *   4. Send the captured buffer, re-enable the RCLK interrupt, go back
 *      to step 1.
 *
 * @param send_queue  queue shared with the serial send task
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t display_raw_capture_spi_init(QueueHandle_t *send_queue);

#ifdef __cplusplus
}
#endif

#endif // _RAW_CAPTURE_SPI_H_
