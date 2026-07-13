#ifndef _FILE_INCLUDE_H_
#define _FILE_INCLUDE_H_

#include "esp_err.h"
#include "device.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    display_type_t display;
    data_error_t error_mask;
}settings_t;

extern settings_t settings;
/**
 * Initialise sample
 *
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t load_system_settings(void);

bool exist_system_settings(void);

esp_err_t save_system_settings(void);

void load_default_system_settings();

void init_system_settings();

#ifdef __cplusplus
}
#endif

#endif // _FILE_INCLUDE_H_