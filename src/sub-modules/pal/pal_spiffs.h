/**
 * @file pal_spiffs.h
 * @brief Platform Abstraction Layer - SPIFFS Interface
 * 
 * This header defines a platform-independent interface for SPIFFS operations.
 * Different platforms can provide their own implementations.
 */

#ifndef PAL_SPIFFS_H
#define PAL_SPIFFS_H

#include "pal_types.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              STRUCTURES                                   */
/*===========================================================================*/

/**
 * @brief SPIFFS configuration structure
 */
typedef struct {
    const char* base_path;              ///< Mount point path (e.g., "/spiffs")
    const char* partition_label;        ///< Partition label (NULL for default)
    uint32_t max_files;                 ///< Maximum number of open files
    bool format_if_mount_failed;        ///< Format if mount fails
} pal_spiffs_config_t;

/**
 * @brief SPIFFS information structure
 */
typedef struct {
    size_t total_bytes;                 ///< Total SPIFFS size in bytes
    size_t used_bytes;                  ///< Used space in bytes
    size_t free_bytes;                  ///< Free space in bytes
    bool is_initialized;                ///< Initialization status
} pal_spiffs_info_t;

/*===========================================================================*/
/*                       INITIALIZATION FUNCTIONS                            */
/*===========================================================================*/

/**
 * @brief Initialize SPIFFS filesystem
 * 
 * @param config Configuration structure
 * @param info Output info structure (can be NULL)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_init(const pal_spiffs_config_t* config, pal_spiffs_info_t* info);

/**
 * @brief Deinitialize SPIFFS filesystem
 * 
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_deinit(void);

/*===========================================================================*/
/*                        FILE OPERATIONS                                    */
/*===========================================================================*/

/**
 * @brief Write data to a file (overwrites existing content)
 * 
 * @param path File path (relative to SPIFFS mount point)
 * @param data Data to write
 * @param size Size of data
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_file_write(const char* path, const uint8_t * data, size_t size);

/**
 * @brief Append data to a file (creates if doesn't exist)
 * 
 * @param path File path (relative to SPIFFS mount point)
 * @param data Data to append
 * @param size Size of data
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_file_append(const char* path, const uint8_t* data, size_t size);

/**
 * @brief Read file content
 * 
 * @param path File path (relative to SPIFFS mount point)
 * @param buffer Buffer to store content
 * @param max_size Maximum buffer size
 * @param bytes_read Output: actual bytes read (can be NULL)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_file_read(const char* path, uint8_t * buffer, size_t max_size, size_t* bytes_read);

/**
 * @brief Read file content starting at a byte offset (for chunked streaming)
 *
 * @param path      File path (relative to SPIFFS mount point)
 * @param offset    Byte offset to start reading from
 * @param buffer    Buffer to store content
 * @param max_size  Maximum bytes to read
 * @param bytes_read Output: actual bytes read (can be NULL)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_file_read_at(const char* path, size_t offset, uint8_t *buffer, size_t max_size, size_t *bytes_read);

/**
 * @brief Check if file exists
 * 
 * @param path File path (relative to SPIFFS mount point)
 * @param exists Output: true if file exists
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_file_exists(const char* path, bool* exists);

/**
 * @brief Delete a file
 * 
 * @param path File path (relative to SPIFFS mount point)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_file_delete(const char* path);

/**
 * @brief Create an empty file
 * 
 * @param path File path (relative to SPIFFS mount point)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_file_create(const char* path);

/**
 * @brief Get file size
 * 
 * @param path File path (relative to SPIFFS mount point)
 * @param size Output: file size in bytes
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_file_get_size(const char* path, size_t* size);

int32_t pal_spiffs_raw_open(const char* filepath, void** handler, uint32_t timeout_ms, const char * mode);
int32_t pal_spiffs_raw_read(void* handler, char content[], size_t* content_size, uint32_t timeout_ms);
int32_t pal_spiffs_raw_write(void* handler, const char content[], size_t* content_size, uint32_t timeout_ms);
int32_t pal_spiffs_raw_close(void* handler, uint32_t timeout_ms);

/*===========================================================================*/
/*                      FILESYSTEM OPERATIONS                                */
/*===========================================================================*/

/**
 * @brief Get filesystem information
 * 
 * @param info Output: filesystem information
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_get_info(pal_spiffs_info_t* info);

/**
 * @brief Format the SPIFFS partition
 * 
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_spiffs_format(void);

#ifdef __cplusplus
}
#endif

#endif // PAL_SPIFFS_H
