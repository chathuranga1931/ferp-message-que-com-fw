#ifndef _SANKI_6_DIGIT_H_
#define _SANKI_6_DIGIT_H_

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
esp_err_t display_sanki_6_digit_init(QueueHandle_t *send_queue);

#ifdef __cplusplus
}
#endif

#endif // _SANKI_6_DIGIT_H_