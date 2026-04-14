#ifndef _CMD_ESP07_H_
#define _CMD_ESP07_H_

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


esp_err_t esp07_get_fw_version(char *ver);

esp_err_t esp07_get_fw_name();

esp_err_t esp07_get_fw_timedate();

esp_err_t esp07_set_display_type(display_type_t type);

esp_err_t esp07_get_inputs(uint32_t *pins);

esp_err_t esp07_set_inputenable(bool level);

#ifdef __cplusplus
}
#endif

#endif // _CMD_ESP07_H_