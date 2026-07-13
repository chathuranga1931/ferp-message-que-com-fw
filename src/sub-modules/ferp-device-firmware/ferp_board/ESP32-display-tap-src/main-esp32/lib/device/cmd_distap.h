#ifndef _CMD_DISTAP_H_
#define _CMD_DISTAP_H_

#include "esp_err.h"
#include "display_types.h"

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


esp_err_t distap_get_fw_version(char *ver);

esp_err_t distap_get_fw_name();

esp_err_t distap_get_fw_timedate();

esp_err_t distap_set_display_type(const display_type_t type);

esp_err_t distap_get_inputs(input_pin_t *pins);

esp_err_t distap_set_inputenable(bool level);

esp_err_t distap_set_led_enable(bool level);

esp_err_t distap_set_cs1_enable(bool level);

esp_err_t distap_set_cs2_enable(bool level);

esp_err_t distap_set_err_mask(const data_error_t err);

#ifdef __cplusplus
}
#endif

#endif // _CMD_DISTAP_H_