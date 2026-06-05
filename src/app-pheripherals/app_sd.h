// app_sd.h
//
// Application SD-card peripheral wrapper.
//
// Provides mutex-protected file and directory I/O over pal_sd.
// All public functions are safe to call from any task concurrently.
//
// Ownership / responsibility split:
//   app_sd    — mutex, init, all file/dir I/O (read/write/append/delete/list)
//   module_sd — HSYS module: calls app_sd_init(), fires MsgSdReady

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pal_sd.h"
#include "storage.h"   // storage_interface_t

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

#define APP_SD_OK               (0)
#define APP_SD_ERR_NOT_INIT     (-1)
#define APP_SD_ERR_BUSY         (-2)
#define APP_SD_ERR_NOT_FOUND    (-3)
#define APP_SD_ERR_IO           (-4)
#define APP_SD_ERR_TOO_LARGE    (-5)
#define APP_SD_ERR_INVALID      (-6)
#define APP_SD_ERR_TIMEOUT      (-7)

// ---------------------------------------------------------------------------
// app_sd_info_t
// ---------------------------------------------------------------------------

typedef struct {
    uint64_t card_size_mb;
    char     card_type[32];
} app_sd_info_t;

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * @brief  Initialise the SD card and internal mutex.
 *
 * Must be called once before any other app_sd_* function.
 * @param config  SPI pin configuration (must not be NULL on ESP-IDF).
 * @param info    Optional — filled with card details on success.
 *
 * @return APP_SD_OK on success, negative error code otherwise.
 */
int32_t app_sd_init(const pal_sd_config_t *config, app_sd_info_t *info);

/**
 * @brief  Query free space on the SD card.
 *
 * @param free_mb  Receives free megabytes; 0 on failure.
 * @return APP_SD_OK on success, negative error code otherwise.
 */
int32_t app_sd_get_free_mb(uint64_t *free_mb);

// ---------------------------------------------------------------------------
// File I/O — all functions are mutex-protected with timeout_ms
// ---------------------------------------------------------------------------

/** Write (overwrite) a file. */
int32_t app_sd_write_file(const char *path, const char *content,
                           uint32_t timeout_ms);

/** Append a line (adds '\n') to a file, creating it if necessary. */
int32_t app_sd_append_line(const char *path, const char *line,
                            uint32_t timeout_ms);

/** Read entire file into buffer (NUL-terminated). */
int32_t app_sd_read_file(const char *path, char *buffer, size_t buf_size,
                          size_t *bytes_read, uint32_t timeout_ms);

/** Read specific line from file (0-based). */
int32_t app_sd_read_line(const char *path, uint32_t line_number,
                          char *buffer, size_t max_len, uint32_t timeout_ms);

/** Create an empty file (no-op if already exists). */
int32_t app_sd_create_file(const char *path, uint32_t timeout_ms);

/** Delete a file (succeeds silently if not found). */
int32_t app_sd_delete_file(const char *path, uint32_t timeout_ms);

// ---------------------------------------------------------------------------
// Directory I/O
// ---------------------------------------------------------------------------

/** Create a directory (and all parents). */
int32_t app_sd_create_dir(const char *path, uint32_t timeout_ms);

/** Remove an empty directory. */
int32_t app_sd_remove_dir(const char *path, uint32_t timeout_ms);

/**
 * @brief  Returns a pointer to a static storage_interface_t whose function
 *         pointers are wired to the app_sd_* file I/O functions.
 *         Valid to call at any time; the pointer is always the same object.
 */
const storage_interface_t *app_sd_get_storage_interface(void);

/**
 * @brief Iterate over files in a directory.
 *
 * Call with *handle = NULL to start; call repeatedly until *has_more is
 * false.  The caller must call app_sd_dir_close() when done.
 */
int32_t app_sd_dir_open(const char *path, pal_sd_dir_handle_t *handle,
                         uint32_t timeout_ms);
int32_t app_sd_dir_read_next(pal_sd_dir_handle_t handle,
                              pal_sd_dir_entry_t *entry, bool *has_more);
int32_t app_sd_dir_close(pal_sd_dir_handle_t handle);

/**
 * @brief  Delete every file and sub-directory on the SD card.
 *
 * Walks the entire card from the root, deleting files and empty directories
 * depth-first.  Stops early and returns APP_SD_ERR_TIMEOUT if the operation
 * exceeds @p total_timeout_ms milliseconds (wall-clock time since the call).
 *
 * @param total_timeout_ms  Maximum allowed duration in milliseconds.
 *                          Pass 0 to use the default (10 minutes).
 * @return APP_SD_OK on success, APP_SD_ERR_TIMEOUT if the time limit was
 *         reached, or another negative error code on failure.
 */
int32_t app_sd_cleanup(uint32_t total_timeout_ms);

#ifdef __cplusplus
}
#endif
