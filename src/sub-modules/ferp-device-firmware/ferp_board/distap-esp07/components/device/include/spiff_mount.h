#ifndef _SPIFF_MOUNT_H_
#define _SPIFF_MOUNT_H_

#include "stdbool.h"
#include "esp_err.h"
#define FILE_BASEPATH "/spiffs"

#ifdef __cplusplus
extern "C"
{
#endif


/**
 * Register and mout SPIFFS from VFS
 *
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - ESP_ERR_INVALID_STATE already unregistered
 */
esp_err_t mount_file_system();

bool local_storage_file_exist(const char* key);

esp_err_t local_storage_set(const char* key, const uint8_t* buffer, size_t length);

esp_err_t local_storage_get(const char* key, uint8_t* buffer, size_t* length);

#ifdef __cplusplus
}
#endif

#endif // _SPIFF_MOUNT_H_