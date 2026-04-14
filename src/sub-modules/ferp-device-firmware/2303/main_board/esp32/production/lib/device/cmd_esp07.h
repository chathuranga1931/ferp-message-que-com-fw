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

void esp07_get_fw_name();

void esp07_get_fw_timedate();

void esp07_set_display_type(const display_type_t type);

void esp07_set_err_mask(const data_error_t err);

#ifdef __cplusplus
}
#endif

#endif // _CMD_ESP07_H_