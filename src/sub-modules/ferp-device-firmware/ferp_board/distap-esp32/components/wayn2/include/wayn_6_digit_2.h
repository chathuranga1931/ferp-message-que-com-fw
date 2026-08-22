#ifndef _WAYN_6_DIGIT_2_H_
#define _WAYN_6_DIGIT_2_H_

#include "esp_err.h"
#include "freertos/queue.h"
#include "device.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Initialise Wayne Type02 display capture (Volume/Total only — unit price
 * not decoded yet).
 *
 * @param send_queue  queue shared with the serial send task
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t display_wayne_6_digit_2_init(QueueHandle_t *send_queue);

#ifdef __cplusplus
}
#endif

#endif // _WAYN_6_DIGIT_2_H_
