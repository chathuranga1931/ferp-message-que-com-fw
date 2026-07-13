#ifndef _HONGYANG_8_DIGIT_H_
#define _HONGYANG_8_DIGIT_H_

#include "esp_err.h"
#include "freertos/queue.h"
#include "device.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Initialise sample
 *
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t display_hongyang_8_digit_init(QueueHandle_t *send_queue);

#ifdef __cplusplus
}
#endif

#endif // _HONGYANG_8_DIGIT_H_
