// pal_nvs.h
//
// Platform Abstraction Layer — Non-Volatile Storage (NVS)
//
// Provides a thin, platform-independent interface over key-value NVS.
// On ESP-IDF this maps directly to the NVS flash library.
// On the Mac/PC simulator it maps to binary files under SPIFFS/nvs/.
//
// Namespace and key strings must not exceed 15 characters (NVS limit).

#pragma once

#include "pal_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief  Initialise the NVS storage backend.
 *
 * On ESP-IDF: calls nvs_flash_init(), erasing if no free pages or version
 * mismatch.  On Mac: creates the SPIFFS/nvs/ directory tree.
 * Safe to call multiple times (idempotent after first success).
 *
 * @return PAL_OK on success, PAL_ERROR_IO on failure.
 */
int32_t pal_nvs_init(void);

// ---------------------------------------------------------------------------
// Integer (int64)
// ---------------------------------------------------------------------------

/**
 * @brief  Write a signed 64-bit integer.
 *
 * @param  ns    Namespace string (max 15 chars)
 * @param  key   Key string (max 15 chars)
 * @param  value Value to store
 * @return PAL_OK on success
 */
int32_t pal_nvs_write_i64(const char *ns, const char *key, int64_t value);

/**
 * @brief  Read a signed 64-bit integer.
 *
 * @param  ns    Namespace string
 * @param  key   Key string
 * @param  value OUT — populated on success
 * @return PAL_OK on success, PAL_ERROR_NOT_FOUND if key does not exist
 */
int32_t pal_nvs_read_i64(const char *ns, const char *key, int64_t *value);

// ---------------------------------------------------------------------------
// Blob (arbitrary bytes)
// ---------------------------------------------------------------------------

/**
 * @brief  Write a binary blob.
 *
 * @param  ns    Namespace string
 * @param  key   Key string
 * @param  data  Pointer to data
 * @param  size  Number of bytes to write
 * @return PAL_OK on success
 */
int32_t pal_nvs_write_blob(const char *ns, const char *key,
                            const void *data, size_t size);

/**
 * @brief  Read a binary blob.
 *
 * @param  ns          Namespace string
 * @param  key         Key string
 * @param  buf         Caller-supplied buffer
 * @param  max_size    Buffer capacity in bytes
 * @param  bytes_read  OUT — actual bytes read (may be NULL)
 * @return PAL_OK on success, PAL_ERROR_NOT_FOUND if key does not exist,
 *         PAL_ERROR_OVERFLOW if stored blob exceeds max_size
 */
int32_t pal_nvs_read_blob(const char *ns, const char *key,
                           void *buf, size_t max_size, size_t *bytes_read);

// ---------------------------------------------------------------------------
// Erase
// ---------------------------------------------------------------------------

/**
 * @brief  Erase a single key.  Returns PAL_OK if the key did not exist.
 */
int32_t pal_nvs_erase_key(const char *ns, const char *key);

#ifdef __cplusplus
}
#endif
