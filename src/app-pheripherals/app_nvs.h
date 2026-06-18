// app_nvs.h
//
// Application NVS peripheral wrapper.
//
// Thin, init-guarded wrapper over pal_nvs.  Unlike app_spiffs, no mutex is
// needed here because the ESP-IDF NVS library is internally thread-safe.
//
// Ownership / responsibility split:
//   app_nvs  — init guard, logging, simple API
//   pal_nvs  — platform-specific storage (flash / host file)

#pragma once

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

#define APP_NVS_OK              (0)
#define APP_NVS_ERR_NOT_INIT    (-1)
#define APP_NVS_ERR_NOT_FOUND   (-2)
#define APP_NVS_ERR_IO          (-3)
#define APP_NVS_ERR_OVERFLOW    (-4)
#define APP_NVS_ERR_INVALID     (-5)

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief  Initialise the NVS backend (idempotent).
 *
 * Must be called once before any other app_nvs_* function.
 * On ESP-IDF this calls nvs_flash_init(); on Mac it creates the emulation
 * directory.  Safe to call from any module init().
 *
 * @return APP_NVS_OK on success, negative error code otherwise.
 */
int32_t app_nvs_init(void);

// ---------------------------------------------------------------------------
// Integer (int64)
// ---------------------------------------------------------------------------

/**
 * @brief  Write a signed 64-bit integer.
 *
 * @param  ns    Namespace (max 15 chars)
 * @param  key   Key (max 15 chars)
 * @param  value Value to store
 * @return APP_NVS_OK on success
 */
int32_t app_nvs_write_i64(const char *ns, const char *key, int64_t value);

/**
 * @brief  Read a signed 64-bit integer.
 *
 * @param  ns    Namespace
 * @param  key   Key
 * @param  value OUT — populated on success
 * @return APP_NVS_OK on success, APP_NVS_ERR_NOT_FOUND if key absent
 */
int32_t app_nvs_read_i64(const char *ns, const char *key, int64_t *value);

// ---------------------------------------------------------------------------
// Blob (arbitrary bytes)
// ---------------------------------------------------------------------------

/**
 * @brief  Write a binary blob.
 *
 * @param  ns    Namespace
 * @param  key   Key
 * @param  data  Data to store
 * @param  size  Number of bytes
 * @return APP_NVS_OK on success
 */
int32_t app_nvs_write_blob(const char *ns, const char *key,
                            const void *data, size_t size);

/**
 * @brief  Read a binary blob.
 *
 * @param  ns          Namespace
 * @param  key         Key
 * @param  buf         Caller-supplied buffer
 * @param  max_size    Buffer capacity in bytes
 * @param  bytes_read  OUT — actual bytes read (may be NULL)
 * @return APP_NVS_OK on success, APP_NVS_ERR_NOT_FOUND if key absent,
 *         APP_NVS_ERR_OVERFLOW if blob exceeds max_size
 */
int32_t app_nvs_read_blob(const char *ns, const char *key,
                           void *buf, size_t max_size, size_t *bytes_read);

#ifdef __cplusplus
}
#endif
