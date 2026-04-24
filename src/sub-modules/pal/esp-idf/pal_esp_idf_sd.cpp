/**
 * @file pal_esp_idf_sd.cpp
 * @brief Platform Abstraction Layer - ESP-IDF SD Card Implementation
 * 
 * This file implements the SD card interface for ESP-IDF platform using
 * SDSPI driver and FATFS filesystem.
 */

#include "pal_sd.h"
#include "pal_logger.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "ff.h"

/*===========================================================================*/
/*                            DEFINITIONS                                    */
/*===========================================================================*/

#define SD_MOUNT_POINT "/sdcard"
#define __TAG__ "PAL_SD  "

#define SD_DEBUG_LOG_EN      LOG_DIS

/*===========================================================================*/
/*                           STATIC VARIABLES                                */
/*===========================================================================*/

static sdmmc_card_t* sd_card = NULL;
static bool is_initialized = false;

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
        snprintf(full_path, max_len, "%s%s", SD_MOUNT_POINT, relative_path);
    } else {
        snprintf(full_path, max_len, "%s/%s", SD_MOUNT_POINT, relative_path);
    }
}

/*===========================================================================*/
/*                       INITIALIZATION FUNCTIONS                            */
/*===========================================================================*/

int32_t pal_sd_init(const pal_sd_config_t* config, pal_sd_info_t* info) {
    if(config == NULL || info == NULL) {
        LOG_MSG_ERROR(SD_DEBUG_LOG_EN, "Invalid parameters");
        return PAL_ERROR_INVALID;
    }
    
    if(is_initialized) {
        LOG_MSG_ERROR(SD_DEBUG_LOG_EN, "SD card already initialized");
        return PAL_OK;
    }
    
    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = config->mosi_pin,
        .miso_io_num = config->miso_pin,
        .sclk_io_num = config->sck_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    // Initialize SPI bus
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    esp_err_t ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        LOG_MSG_ERROR(SD_DEBUG_LOG_EN, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return PAL_ERROR_INIT;
    }
    
    // Configure SD card slot
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = (gpio_num_t)config->cs_pin;
    slot_config.host_id = (spi_host_device_t)host.slot;
    
    // Mount filesystem configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // Mount filesystem
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &sd_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            LOG_MSG_ERROR(SD_DEBUG_LOG_EN, "Failed to mount filesystem");
        } else {
            LOG_MSG_ERROR(SD_DEBUG_LOG_EN, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        spi_bus_free((spi_host_device_t)host.slot);
        return PAL_ERROR_INIT;
    }
    
    // Get card information
    sdmmc_card_print_info(stdout, sd_card);
    
    // Fill info structure
    info->card_size_mb = ((uint64_t)sd_card->csd.capacity) * sd_card->csd.sector_size / (1024 * 1024);
    
    // Check if card is SDHC/SDXC (High Capacity) or SDSC (Standard Capacity)
    // SDHC/SDXC cards have capacity in the CSD register and use block addressing
    if (sd_card->csd.capacity > 0) {
        snprintf(info->card_type, sizeof(info->card_type), "SDHC/SDXC");
    } else {
        snprintf(info->card_type, sizeof(info->card_type), "SDSC");
    }
    
    info->is_initialized = true;
    is_initialized = true;
    
    LOG_MSG_INFO(SD_DEBUG_LOG_EN, "SD card initialized: %s, %lluMB", info->card_type, info->card_size_mb);
    
    return PAL_OK;
}

int32_t pal_sd_deinit(void) {
    if(!is_initialized) {
        return PAL_OK;
    }
    
    if(sd_card != NULL) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sd_card);
        sd_card = NULL;
    }
    
    is_initialized = false;
    LOG_MSG_INFO(SD_DEBUG_LOG_EN, "SD card deinitialized");
    
    return PAL_OK;
}

/*===========================================================================*/
/*                        FILE OPERATIONS                                    */
/*===========================================================================*/

int32_t pal_sd_file_write(const char* path, const char* data, size_t size) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || data == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    FILE* file = fopen(full_path, "w");
    if(file == NULL) {
        LOG_MSG_ERROR(SD_DEBUG_LOG_EN, "Failed to open file for writing: %s", full_path);
        return PAL_ERROR_IO;
    }
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    if(written != size) {
        LOG_MSG_ERROR(SD_DEBUG_LOG_EN, "Failed to write complete data");
        return PAL_ERROR_IO;
    }
    
    return PAL_OK;
}

int32_t pal_sd_file_append(const char* path, const char* data, size_t size) {
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
        LOG_MSG_ERROR(SD_DEBUG_LOG_EN, "Failed to open file: %s", full_path);
        return PAL_ERROR_IO;
    }
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    if(written != size) {
        return PAL_ERROR_IO;
    }
    
    return PAL_OK;
}

int32_t pal_sd_file_read(const char* path, char* buffer, size_t max_size, size_t* bytes_read) {
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
    
    fclose(file);
    
    if(bytes_read != NULL) {
        *bytes_read = read_size;
    }
    
    return PAL_OK;
}

int32_t pal_sd_file_read_line(const char* path, uint32_t line_number, char* buffer, size_t max_size) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || buffer == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    FILE* file = fopen(full_path, "r");
    if(file == NULL) {
        return PAL_ERROR_IO;
    }
    
    // Skip lines until target line
    char temp_buffer[256];
    for(uint32_t i = 0; i < line_number; i++) {
        if(fgets(temp_buffer, sizeof(temp_buffer), file) == NULL) {
            fclose(file);
            return PAL_ERROR_NOT_FOUND;
        }
    }
    
    // Read target line
    if(fgets(buffer, max_size, file) != NULL) {
        // Remove trailing newline
        size_t len = strlen(buffer);
        if(len > 0 && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) {
            buffer[len-1] = '\0';
            if(len > 1 && buffer[len-2] == '\r') {
                buffer[len-2] = '\0';
            }
        }
    } else {
        fclose(file);
        return PAL_ERROR_NOT_FOUND;
    }
    
    fclose(file);
    return PAL_OK;
}

int32_t pal_sd_file_exists(const char* path, bool* exists) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || exists == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    struct stat st;
    *exists = (stat(full_path, &st) == 0);
    
    return PAL_OK;
}

int32_t pal_sd_file_delete(const char* path) {
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

int32_t pal_sd_file_create(const char* path) {
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

int32_t pal_sd_file_get_size(const char* path, size_t* size) {
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

/*===========================================================================*/
/*                      DIRECTORY OPERATIONS                                 */
/*===========================================================================*/

/**
 * @brief Internal directory context structure
 */
typedef struct {
    DIR* dir;
    char current_path[256];
} pal_sd_dir_context_t;

int32_t pal_sd_dir_open(const char* path, pal_sd_dir_handle_t* handle) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL || handle == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    DIR* dir = opendir(full_path);
    if(dir == NULL) {
        return PAL_ERROR_NOT_FOUND;
    }
    
    // Allocate context
    pal_sd_dir_context_t* ctx = (pal_sd_dir_context_t*)malloc(sizeof(pal_sd_dir_context_t));
    if(ctx == NULL) {
        closedir(dir);
        return PAL_ERROR;
    }
    
    ctx->dir = dir;
    strncpy(ctx->current_path, full_path, sizeof(ctx->current_path));
    
    *handle = (pal_sd_dir_handle_t)ctx;
    return PAL_OK;
}

int32_t pal_sd_dir_read_next(pal_sd_dir_handle_t handle, pal_sd_dir_entry_t* entry, bool* has_more) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(handle == NULL || entry == NULL || has_more == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    pal_sd_dir_context_t* ctx = (pal_sd_dir_context_t*)handle;
    
    struct dirent* dirent_entry = readdir(ctx->dir);
    if(dirent_entry == NULL) {
        *has_more = false;
        return PAL_OK;
    }
    
    // Skip "." and ".." entries
    while(dirent_entry != NULL && 
          (strcmp(dirent_entry->d_name, ".") == 0 || strcmp(dirent_entry->d_name, "..") == 0)) {
        dirent_entry = readdir(ctx->dir);
    }
    
    if(dirent_entry == NULL) {
        *has_more = false;
        return PAL_OK;
    }
    
    // Fill entry information
    strncpy(entry->name, dirent_entry->d_name, sizeof(entry->name));
    snprintf(entry->full_path, sizeof(entry->full_path), "%s/%s", ctx->current_path, dirent_entry->d_name);
    
    // Get file stats
    struct stat st;
    if(stat(entry->full_path, &st) == 0) {
        entry->is_directory = S_ISDIR(st.st_mode);
        entry->size = st.st_size;
    } else {
        entry->is_directory = false;
        entry->size = 0;
    }
    
    *has_more = true;
    return PAL_OK;
}

int32_t pal_sd_dir_close(pal_sd_dir_handle_t handle) {
    if(handle == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    pal_sd_dir_context_t* ctx = (pal_sd_dir_context_t*)handle;
    
    if(ctx->dir != NULL) {
        closedir(ctx->dir);
    }
    
    free(ctx);
    return PAL_OK;
}

int32_t pal_sd_dir_remove(const char* path) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    if(rmdir(full_path) != 0) {
        return PAL_ERROR_IO;
    }
    
    return PAL_OK;
}

int32_t pal_sd_dir_create(const char* path) {
    if(!is_initialized) {
        return PAL_ERROR_INIT;
    }
    
    if(path == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    char full_path[256];
    build_full_path(path, full_path, sizeof(full_path));
    
    if(mkdir(full_path, 0777) != 0) {
        return PAL_ERROR_IO;
    }
    
    return PAL_OK;
}

int32_t pal_sd_get_free_mb(uint64_t *free_mb) {
    if(!is_initialized || sd_card == NULL) {
        return PAL_ERROR_INIT;
    }
    if(free_mb == NULL) {
        return PAL_ERROR_INVALID;
    }
    FATFS *fs;
    DWORD free_clusters;
    if(f_getfree("0:", &free_clusters, &fs) != FR_OK) {
        return PAL_ERROR_IO;
    }
    uint64_t sector_size = sd_card->csd.sector_size ? sd_card->csd.sector_size : 512;
    *free_mb = ((uint64_t)free_clusters * fs->csize * sector_size) / (1024 * 1024);
    return PAL_OK;
}
