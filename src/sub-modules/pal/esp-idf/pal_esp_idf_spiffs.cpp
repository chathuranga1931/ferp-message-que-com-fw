/**
 * @file pal_esp_idf_spiffs.cpp
 * @brief Platform Abstraction Layer - ESP-IDF SPIFFS Implementation
 * 
 * This file implements the SPIFFS interface for ESP-IDF platform using
 * the ESP VFS SPIFFS driver.
 */

#include "pal_spiffs.h"
#include "pal_logger.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_spiffs.h"
#include <errno.h>

/*===========================================================================*/
/*                            DEFINITIONS                                    */
/*===========================================================================*/

#define __TAG__ "PAL_SPIF"
#define DEFAULT_BASE_PATH "/spiffs"

#define SPIF_DEBUG_LOG_EN      LOG_DIS
#define SPIF_WARN_LOG_EN       LOG_DIS
#define SPIF_ERROR_LOG_EN      LOG_EN
#define SPIF_INFO_LOG_EN       LOG_DIS

/*===========================================================================*/
/*                           STATIC VARIABLES                                */
/*===========================================================================*/

static bool is_initialized = false;
static char base_path[32] = DEFAULT_BASE_PATH;

/*===========================================================================*/
/*                          HELPER FUNCTIONS                                 */
/*===========================================================================*/

/**
 * @brief Build full path from relative path
 */
static void build_full_path(const char* relative_path, char* full_path, size_t max_len) {
    if(relative_path == NULL || full_path == NULL) {
        return;
    }
    
    if(relative_path[0] == '/') {
        snprintf(full_path, max_len, "%s%s", base_path, relative_path);
    } else {
        snprintf(full_path, max_len, "%s/%s", base_path, relative_path);
    }
}

/*===========================================================================*/
/*                       INITIALIZATION FUNCTIONS                            */
/*===========================================================================*/

int32_t pal_spiffs_init(const pal_spiffs_config_t* config, pal_spiffs_info_t* info) {
    if(config == NULL) {
        LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Invalid configuration");
        return PAL_ERROR_INVALID;
    }
    
    if(is_initialized) {
        LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "SPIFFS already initialized");
        return PAL_OK;
    }
    
    // Store base path
    if(config->base_path != NULL) {
        strncpy(base_path, config->base_path, sizeof(base_path) - 1);
        base_path[sizeof(base_path) - 1] = '\0';
    }
    
    // Configure SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = config->partition_label,
        .max_files = config->max_files,
        .format_if_mount_failed = config->format_if_mount_failed
    };
    
    // Register and mount SPIFFS
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if(ret != ESP_OK) {
        if(ret == ESP_FAIL) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to mount or format filesystem");
        } else if(ret == ESP_ERR_NOT_FOUND) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to find SPIFFS partition");
        } else {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to initialize SPIFFS: %s", esp_err_to_name(ret));
        }
        return PAL_ERROR_INIT;
    }
    
    is_initialized = true;
    
    // Get filesystem info if requested
    if(info != NULL) {
        size_t total = 0, used = 0;
        ret = esp_spiffs_info(config->partition_label, &total, &used);
        if(ret == ESP_OK) {
            info->total_bytes = total;
            info->used_bytes = used;
            info->free_bytes = total - used;
            info->is_initialized = true;
            
            LOG_MSG_INFO(SPIF_INFO_LOG_EN, "SPIFFS initialized: Total=%zu bytes, Used=%zu bytes, Free=%zu bytes", 
                     total, used, total - used);
        } else {
            info->is_initialized = true;
            info->total_bytes = 0;
            info->used_bytes = 0;
            info->free_bytes = 0;
        }
    }
    
    LOG_MSG_INFO(SPIF_INFO_LOG_EN, "SPIFFS mounted at %s", base_path);
    
    return PAL_OK;
}

int32_t pal_spiffs_deinit(void) {
    if(!is_initialized) {
        return PAL_OK;
    }
    
    esp_err_t ret = esp_vfs_spiffs_unregister(NULL);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to unregister SPIFFS: %s", esp_err_to_name(ret));
        return PAL_ERROR;
    }
    
    is_initialized = false;
    LOG_MSG_INFO(SPIF_INFO_LOG_EN, "SPIFFS unmounted");
    
    return PAL_OK;
}

/*===========================================================================*/
/*                        FILE OPERATIONS                                    */
/*===========================================================================*/

int32_t pal_spiffs_file_write(const char* path, const uint8_t * data, size_t size) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || data == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));

    size_t _total = 0, _used = 0;
    esp_spiffs_info(NULL, &_total, &_used);
    LOG_MSG_DEBUG(SPIF_DEBUG_LOG_EN, "write: '%s' size=%zu | spiffs total=%zu used=%zu free=%zu",
                  full_path, size, _total, _used, _total - _used);

    FILE* file = fopen(full_path, "w");
    if(file == NULL) {
        LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "fopen failed for '%s': errno=%d (%s)", full_path, errno, strerror(errno));
        return PAL_ERROR_IO;
    }
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    if(written != size) {
        LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "fwrite incomplete: wrote=%zu expected=%zu for '%s'", written, size, full_path);
        return PAL_ERROR_IO;
    }
    
    return PAL_OK;
}

int32_t pal_spiffs_file_append(const char* path, const uint8_t * data, size_t size) 
{   
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || data == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    // Check if file exists
    struct stat st;
    bool exists = (stat(full_path, &st) == 0);
    
    // Open for writing (create) or appending
    FILE* file = fopen(full_path, exists ? "a" : "w");
    if(file == NULL) {
        LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to open file: %s", full_path);
        return PAL_ERROR_IO;
    }
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    if(written != size) {
        return PAL_ERROR_IO;
    }
    
    return PAL_OK;
}

int32_t pal_spiffs_file_read(const char* path, uint8_t * buffer, size_t max_size, size_t* bytes_read) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || buffer == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    // Check if file exists
    struct stat st;
    if(stat(full_path, &st) != 0) {
        return PAL_ERROR_NOT_FOUND;
    }
    
    FILE* file = fopen(full_path, "r");
    if(file == NULL) {
        return PAL_ERROR_IO;
    }
    
    memset(buffer, 0, max_size);
    size_t read_size = fread(buffer, 1, max_size - 1, file);
    buffer[read_size] = '\0';

    LOG_MSG_DEBUG(SPIF_DEBUG_LOG_EN, "read path: '%s' read_content: %s", full_path, buffer);

    fclose(file);
    
    if(bytes_read != NULL) {
        *bytes_read = read_size;
    }
    
    return PAL_OK;
}

int32_t pal_spiffs_file_read_at(const char* path, size_t offset, uint8_t *buffer, size_t max_size, size_t *bytes_read)
{
    if (!is_initialized)             return PAL_ERROR_INIT;
    if (!path || !buffer)            return PAL_ERROR_INVALID;

    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));

    struct stat st;
    if (stat(full_path, &st) != 0)   return PAL_ERROR_NOT_FOUND;

    FILE *file = fopen(full_path, "r");
    if (!file)                       return PAL_ERROR_IO;

    if (offset > 0) fseek(file, (long)offset, SEEK_SET);
    size_t n = fread(buffer, 1, max_size, file);
    fclose(file);

    if (bytes_read) *bytes_read = n;
    return PAL_OK;
}

int32_t pal_spiffs_file_exists(const char* path, bool* exists) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || exists == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    struct stat st;
    int stat_result = stat(full_path, &st);
    *exists = (stat_result == 0);
    
    LOG_MSG_DEBUG(SPIF_DEBUG_LOG_EN, "pal_spiffs_file_exists: path='%s' full='%s' stat=%d exists=%d",
                  path, full_path, stat_result, (int)*exists);
    
    return PAL_OK;
}

int32_t pal_spiffs_file_delete(const char* path) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    if(unlink(full_path) != 0) {
        return PAL_ERROR_IO;
    }
    
    return PAL_OK;
}

int32_t pal_spiffs_file_create(const char* path) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    FILE* file = fopen(full_path, "w");
    if(file == NULL) {
        return PAL_ERROR_IO;
    }
    
    fclose(file);
    return PAL_OK;
}

int32_t pal_spiffs_file_get_size(const char* path, size_t* size) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || size == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    struct stat st;
    if(stat(full_path, &st) != 0) {
        return PAL_ERROR_NOT_FOUND;
    }
    
    *size = st.st_size;
    return PAL_OK;
}

int32_t pal_spiffs_raw_open(const char* filepath, void** handler, uint32_t timeout_ms, const char * mode)
{
    LOG_MSG_DEBUG(SPIF_DEBUG_LOG_EN, "Opening SPIFFS file: %s", filepath);
    bool result = false;
    
    do {
        if (filepath == NULL || handler == NULL || mode == NULL) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Invalid parameters");
            break;
        }
        
        char full_path[256];
        build_full_path(filepath, full_path, sizeof(full_path));
        
        FILE* file = fopen(full_path, mode);
        if (file == NULL) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to open file: %s", full_path);
            break;
        }
        
        *handler = (void*)file;
        result = true;
        
    } while (false);
    
    return result ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_spiffs_raw_close(void* handler, uint32_t timeout_ms) 
{
    LOG_MSG_DEBUG(SPIF_DEBUG_LOG_EN, "Closing SPIFFS file");
    bool result = false;
    
    do {
        if (handler == NULL) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Invalid handler");
            break;
        }
        
        FILE* file = (FILE*)handler;
        fclose(file);
        
        result = true;
        
    } while (false);
    
    return result ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_spiffs_raw_write(void* handler, const char content[], size_t* content_size, uint32_t timeout_ms) 
{
    LOG_MSG_DEBUG(SPIF_DEBUG_LOG_EN, "Writing to SPIFFS file");
    bool result = false;
    
    do {
        if (handler == NULL || content == NULL || content_size == NULL) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Invalid parameters");
            break;
        }
        
        FILE* file = (FILE*)handler;
        size_t written = fwrite(content, 1, *content_size, file);
        
        if (written != *content_size) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to write complete data");
            break;
        }
        
        result = true;
        
    } while (false);
    
    return result ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_spiffs_raw_read(void* handler, char content[], size_t* content_size, uint32_t timeout_ms) 
{
    LOG_MSG_DEBUG(SPIF_DEBUG_LOG_EN, "Reading from SPIFFS file");
    bool result = false;
    
    do {
        if (handler == NULL || content == NULL || content_size == NULL) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Invalid parameters");
            break;
        }
        
        FILE* file = (FILE*)handler;
        size_t read_size = fread(content, 1, *content_size - 1, file);
        
        if (read_size == 0) {
            LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to read data");
            break;
        }
        
        content[read_size] = '\0';
        *content_size = read_size;
        
        result = true;
        
    } while (false);
    
    return result ? PAL_OK : PAL_ERROR_IO;
}

/*===========================================================================*/
/*                      FILESYSTEM OPERATIONS                                */
/*===========================================================================*/

int32_t pal_spiffs_get_info(pal_spiffs_info_t* info) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(info == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(NULL, &total, &used);
    
    if(ret != ESP_OK) {
        return PAL_ERROR;
    }
    
    info->total_bytes = total;
    info->used_bytes = used;
    info->free_bytes = total - used;
    info->is_initialized = is_initialized;
    
    return PAL_OK;
}

int32_t pal_spiffs_format(void) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    esp_err_t ret = esp_spiffs_format(NULL);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(SPIF_ERROR_LOG_EN, "Failed to format SPIFFS: %s", esp_err_to_name(ret));
        return PAL_ERROR;
    }
    
    LOG_MSG_DEBUG(SPIF_DEBUG_LOG_EN, "SPIFFS formatted successfully");
    return PAL_OK;
}
