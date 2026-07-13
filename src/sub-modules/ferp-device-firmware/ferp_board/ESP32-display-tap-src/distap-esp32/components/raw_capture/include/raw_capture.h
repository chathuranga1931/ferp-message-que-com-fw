#ifndef _RAW_CAPTURE_H_
#define _RAW_CAPTURE_H_

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Initialise the generic raw-capture driver used to reverse-engineer
 * unknown pumps (display types >= DIS_RAW_TYPE_BASE, see device.h).
 * Captures both channels' SDATA1 *and* SDATA2 lines, one bit per SCLK
 * edge, latched into a codeword_bits-wide codeword on each RCLK edge — no
 * pump-specific decoding, just raw codewords forwarded to main in
 * fixed-size chunked batches. Each channel+line combination is its own
 * fully independent stream (TX_ID_RAW_DIS1_L1_DATA / _L2 /
 * TX_ID_RAW_DIS2_L1_DATA / _L2).
 *
 * @param send_queue    shared queue feeding serial_send_task (device.c)
 * @param codeword_bits width of one captured codeword: 8 or 12
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t display_raw_capture_init(QueueHandle_t *send_queue, uint8_t codeword_bits);

#ifdef __cplusplus
}
#endif

#endif // _RAW_CAPTURE_H_
