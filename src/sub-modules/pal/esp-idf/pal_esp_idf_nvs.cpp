// pal_esp_idf_nvs.cpp
//
// ESP-IDF implementation of pal_nvs.h.
// Each operation opens a namespace handle, performs the operation, commits
// (writes only), and closes — keeping handles ephemeral and thread-safe.

#include "pal_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"

int32_t pal_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    return (err == ESP_OK) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_nvs_write_i64(const char *ns, const char *key, int64_t value)
{
    if (!ns || !key) return PAL_ERROR_INVALID;

    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return PAL_ERROR_IO;

    err = nvs_set_i64(h, key, value);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    return (err == ESP_OK) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_nvs_read_i64(const char *ns, const char *key, int64_t *value)
{
    if (!ns || !key || !value) return PAL_ERROR_INVALID;

    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return PAL_ERROR_NOT_FOUND;
    if (err != ESP_OK)                return PAL_ERROR_IO;

    err = nvs_get_i64(h, key, value);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) return PAL_ERROR_NOT_FOUND;
    return (err == ESP_OK) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_nvs_write_blob(const char *ns, const char *key,
                            const void *data, size_t size)
{
    if (!ns || !key || !data || size == 0) return PAL_ERROR_INVALID;

    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return PAL_ERROR_IO;

    err = nvs_set_blob(h, key, data, size);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    return (err == ESP_OK) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_nvs_read_blob(const char *ns, const char *key,
                           void *buf, size_t max_size, size_t *bytes_read)
{
    if (!ns || !key || !buf || max_size == 0) return PAL_ERROR_INVALID;

    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return PAL_ERROR_NOT_FOUND;
    if (err != ESP_OK)                return PAL_ERROR_IO;

    size_t sz = max_size;
    err = nvs_get_blob(h, key, buf, &sz);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND)        return PAL_ERROR_NOT_FOUND;
    if (err == ESP_ERR_NVS_INVALID_LENGTH)   return PAL_ERROR_OVERFLOW;
    if (err != ESP_OK)                        return PAL_ERROR_IO;

    if (bytes_read) *bytes_read = sz;
    return PAL_OK;
}

int32_t pal_nvs_erase_key(const char *ns, const char *key)
{
    if (!ns || !key) return PAL_ERROR_INVALID;

    nvs_handle_t h;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &h);
    if (err != ESP_OK) return PAL_ERROR_IO;

    err = nvs_erase_key(h, key);
    if (err == ESP_OK) nvs_commit(h);
    nvs_close(h);

    return (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) ? PAL_OK : PAL_ERROR_IO;
}
