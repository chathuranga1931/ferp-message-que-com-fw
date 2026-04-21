/**
 * @file pal_sd.h
 * @brief Platform Abstraction Layer - SD Card Interface
 * 
 * This file defines a platform-independent interface for SD card operations.
 * Implementations are provided per platform (ESP-IDF, Arduino, Linux, etc.)
 */

#ifndef PAL_SD_H
#define PAL_SD_H

#include "pal_types.h"

/*===========================================================================*/
/*                            CONFIGURATION                                  */
/*===========================================================================*/

/**
 * @brief SD card SPI configuration
 */
typedef struct {
    pal_gpio_num_t cs_pin;      // Chip select pin
    pal_gpio_num_t mosi_pin;    // Master Out Slave In
    pal_gpio_num_t miso_pin;    // Master In Slave Out
    pal_gpio_num_t sck_pin;     // Serial Clock
} pal_sd_config_t;

/**
 * @brief SD card information
 */
typedef struct {
    uint64_t card_size_mb;      // Card size in megabytes
    char card_type[32];         // Card type string (e.g., "SDHC", "SDSC")
    bool is_initialized;        // Initialization status
} pal_sd_info_t;

/**
 * @brief Directory entry information
 */
typedef struct {
    char name[256];             // Entry name (file or directory)
    char full_path[512];        // Full path to entry
    bool is_directory;          // True if directory, false if file
    size_t size;                // File size in bytes (0 for directories)
} pal_sd_dir_entry_t;

/**
 * @brief Opaque directory handle type
 */
typedef void* pal_sd_dir_handle_t;

/*===========================================================================*/
/*                       INITIALIZATION FUNCTIONS                            */
/*===========================================================================*/

/**
 * @brief Initialize SD card subsystem
 * 
 * @param config Pointer to SD card configuration
 * @param info Pointer to structure to receive card information
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_init(const pal_sd_config_t* config, pal_sd_info_t* info);

/**
 * @brief De-initialize SD card subsystem
 * 
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_deinit(void);

/*===========================================================================*/
/*                        FILE OPERATIONS                                    */
/*===========================================================================*/

/**
 * @brief Write data to file (create or overwrite)
 * 
 * @param path Relative file path (e.g., "config.txt" or "/logs/data.txt")
 * @param data Pointer to data to write
 * @param size Size of data in bytes
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_file_write(const char* path, const char* data, size_t size);

/**
 * @brief Append data to file
 * 
 * @param path Relative file path
 * @param data Pointer to data to append
 * @param size Size of data in bytes
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_file_append(const char* path, const char* data, size_t size);

/**
 * @brief Read entire file into buffer
 * 
 * @param path Relative file path
 * @param buffer Buffer to store read data
 * @param max_size Maximum size of buffer
 * @param bytes_read Pointer to store actual bytes read (can be NULL)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_file_read(const char* path, char* buffer, size_t max_size, size_t* bytes_read);

/**
 * @brief Read specific line from file
 * 
 * @param path Relative file path
 * @param line_number Line number to read (0-based)
 * @param buffer Buffer to store line content
 * @param max_size Maximum size of buffer
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_file_read_line(const char* path, uint32_t line_number, char* buffer, size_t max_size);

/**
 * @brief Check if file exists
 * 
 * @param path Relative file path
 * @param exists Pointer to store result (true if exists)
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_file_exists(const char* path, bool* exists);

/**
 * @brief Delete file
 * 
 * @param path Relative file path
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_file_delete(const char* path);

/**
 * @brief Create empty file
 * 
 * @param path Relative file path
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_file_create(const char* path);

/**
 * @brief Get file size
 * 
 * @param path Relative file path
 * @param size Pointer to store file size
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_file_get_size(const char* path, size_t* size);

/*===========================================================================*/
/*                      DIRECTORY OPERATIONS                                 */
/*===========================================================================*/

/**
 * @brief Open directory for iteration
 * 
 * @param path Relative directory path
 * @param handle Pointer to store directory handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_dir_open(const char* path, pal_sd_dir_handle_t* handle);

/**
 * @brief Read next entry in directory
 * 
 * @param handle Directory handle
 * @param entry Pointer to structure to receive entry info
 * @param has_more Pointer to store if more entries exist
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_dir_read_next(pal_sd_dir_handle_t handle, pal_sd_dir_entry_t* entry, bool* has_more);

/**
 * @brief Close directory handle
 * 
 * @param handle Directory handle
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_dir_close(pal_sd_dir_handle_t handle);

/**
 * @brief Remove empty directory
 * 
 * @param path Relative directory path
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_dir_remove(const char* path);

/**
 * @brief Create directory
 * 
 * @param path Relative directory path
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_sd_dir_create(const char* path);

/**
 * @brief Get free space on the SD card (in megabytes).
 *
 * On embedded targets this queries the FAT filesystem.
 * On the macOS simulator this queries the host filesystem that holds SDCARD/.
 *
 * @param free_mb  Pointer to receive free megabytes; set to 0 on failure.
 * @return PAL_OK on success, negative error code otherwise.
 */
int32_t pal_sd_get_free_mb(uint64_t *free_mb);

#endif // PAL_SD_H
