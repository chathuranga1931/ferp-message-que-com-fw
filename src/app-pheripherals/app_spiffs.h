// app_spiffs.h
//
// Application SPIFFS peripheral wrapper.
//
// Provides mutex-protected file I/O over pal_spiffs.  All public functions
// are safe to call from any task concurrently.
//
// Ownership / responsibility split:
//   app_spiffs  — mutex, init, all file I/O (read/write/delete/exists)
//   module_spiffs — HSYS module: calls app_spiffs_init(), fires MSG_SPIFFS_READY

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

#define APP_SPIFFS_OK               (0)
#define APP_SPIFFS_ERR_NOT_INIT     (-1)
#define APP_SPIFFS_ERR_BUSY         (-2)
#define APP_SPIFFS_ERR_NOT_FOUND    (-3)
#define APP_SPIFFS_ERR_IO           (-4)
#define APP_SPIFFS_ERR_TOO_LARGE    (-5)
#define APP_SPIFFS_ERR_INVALID      (-6)

// ---------------------------------------------------------------------------
// app_spiffs_info_t
// ---------------------------------------------------------------------------

typedef struct {
    size_t total_bytes;
    size_t used_bytes;
    size_t free_bytes;
} app_spiffs_info_t;

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief  Initialise SPIFFS and the internal mutex.
 *
 * Must be called once before any other app_spiffs_* function.
 * Mounts at "/spiffs" (max 5 open files, format if mount fails).
 *
 * @return APP_SPIFFS_OK on success, negative error code otherwise.
 */
int32_t app_spiffs_init(void);

// ---------------------------------------------------------------------------
// File I/O — all functions are mutex-protected with timeout_ms
// ---------------------------------------------------------------------------

/**
 * @brief  Write (overwrite) a file with the given content string.
 *
 * @param  path        File path relative to SPIFFS mount (e.g. "Configs/DeviceConfigs.json")
 * @param  content     NUL-terminated string to write
 * @param  timeout_ms  Mutex acquire timeout in milliseconds
 * @return APP_SPIFFS_OK on success
 */
int32_t app_spiffs_write_file(const char *path, const char *content, uint32_t timeout_ms);

/**
 * @brief  Read a file's entire content into a caller-supplied buffer.
 *
 * @param  path        File path relative to SPIFFS mount
 * @param  buffer      Caller-supplied buffer to receive content (NUL-terminated on success)
 * @param  buf_size    Size of buffer in bytes (including space for NUL)
 * @param  bytes_read  OUT — actual bytes read (may be NULL)
 * @param  timeout_ms  Mutex acquire timeout in milliseconds
 * @return APP_SPIFFS_OK on success
 */
int32_t app_spiffs_read_file(const char *path, char *buffer, size_t buf_size,
                             size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief  Read a file starting at a byte offset (for chunked HTTP streaming).
 *         No NUL terminator is appended — buf_size bytes are usable for data.
 *
 * @param  path        File path relative to SPIFFS mount
 * @param  offset      Byte offset to start reading from
 * @param  buffer      Caller-supplied buffer
 * @param  buf_size    Maximum bytes to read
 * @param  bytes_read  OUT — actual bytes read (may be NULL)
 * @param  timeout_ms  Mutex acquire timeout in milliseconds
 * @return APP_SPIFFS_OK on success
 */
int32_t app_spiffs_read_file_at(const char *path, size_t offset, char *buffer,
                                size_t buf_size, size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief  Check whether a file exists.
 *
 * @param  path        File path relative to SPIFFS mount
 * @param  exists      OUT — set to true if the file exists
 * @param  timeout_ms  Mutex acquire timeout in milliseconds
 * @return APP_SPIFFS_OK on success
 */
int32_t app_spiffs_file_exists(const char *path, bool *exists, uint32_t timeout_ms);

/**
 * @brief  Delete a file.
 *
 * @param  path        File path relative to SPIFFS mount
 * @param  timeout_ms  Mutex acquire timeout in milliseconds
 * @return APP_SPIFFS_OK on success, APP_SPIFFS_ERR_NOT_FOUND if file does not exist
 */
int32_t app_spiffs_delete_file(const char *path, uint32_t timeout_ms);

/**
 * @brief  Append raw bytes to a file (creates if it does not exist).
 *
 * @param  path        File path relative to SPIFFS mount
 * @param  data        Bytes to append
 * @param  size        Number of bytes
 * @param  timeout_ms  Mutex acquire timeout in milliseconds
 * @return APP_SPIFFS_OK on success
 */
int32_t app_spiffs_append_file(const char *path, const uint8_t *data, size_t size,
                               uint32_t timeout_ms);

/**
 * @brief  Query filesystem size/usage.
 *
 * @param  info  OUT — populated with total/used/free bytes
 * @return APP_SPIFFS_OK on success
 */
int32_t app_spiffs_get_info(app_spiffs_info_t *info);

#ifdef __cplusplus
}
#endif
