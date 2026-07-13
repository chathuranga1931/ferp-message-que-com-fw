#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "spiff_mount.h"
#include "dirent.h"
// #include "storage_interface.h"

// static const char *TAG = "spiff_mount";

esp_err_t mount_file_system()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = FILE_BASEPATH,
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true};

    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        // if (ret == ESP_FAIL)
        // {
        //     ESP_LOGE(TAG, "Failed to mount or format filesystem");
        // }
        // else if (ret == ESP_ERR_NOT_FOUND)
        // {
        //     ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        // }
        // else
        // {
        //     ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        // }
        return ret;
    }

    // ESP_LOGI(TAG, "Performing SPIFFS_check().");
    // ret = esp_spiffs_check(conf.partition_label);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
    //     // return ret;
    // } else {
    //     ESP_LOGI(TAG, "SPIFFS_check() successful");
    // }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    // if (ret != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    //     return ret;
    // }
    // ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);

    // // list available files on spiff partitions
    // DIR *dir = opendir(FILE_BASEPATH);
    // if (dir == NULL)
    // {
    //     // ESP_LOGE(TAG, "Failed to open SPIFFS partition directory");
    //     return ESP_FAIL;
    // }
    // while (true)
    // {
    //     struct dirent *de = readdir(dir);
    //     if (!de)
    //     {
    //         break;
    //     }
    //     char filepath[200] = {};
    //     strlcpy(filepath, de->d_name,sizeof(filepath));
    //     uint32_t file_size = 0;//local_storage_file_size(filepath);
    //     ESP_LOGI(TAG, "Found file: %s, size:%d", de->d_name, file_size);
    // }
    // closedir(dir);
    
    return ESP_OK;
}

bool local_storage_file_exist(const char* key)
{
    char file_path[200] = {};
    strcpy(file_path, FILE_BASEPATH "/");
    strcat(file_path, key);
    FILE* fptr = NULL;
    fptr = fopen(file_path, "rb");
    if (NULL == fptr) {
        return false;
    }
    fclose(fptr);
    return true;
}

esp_err_t local_storage_set(const char* key, const uint8_t* buffer, size_t length)
{
    if (NULL == key || NULL == buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    char file_path[200] = {};
    strcpy(file_path, FILE_BASEPATH "/");
    strcat(file_path, key);

    FILE* fptr = NULL;
    // ESP_LOGI(TAG, "save file:%s", file_path);
    fptr = fopen(file_path, "wb+");
    if (NULL == fptr) {
        // ESP_LOGE(TAG, "open file error");
        return ESP_ERR_NOT_FOUND;
    } else {
        // ESP_LOGI(TAG, "open file OK");
    }

    int file_len = fwrite(buffer, 1, length, fptr);
    fclose(fptr);
    if (file_len != length) {
        // ESP_LOGE(TAG, "uf_kv_write fail %d", file_len);
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

esp_err_t local_storage_get(const char* key, uint8_t* buffer, size_t* length)
{
    if (NULL == key || NULL == buffer || NULL == length) {
        return ESP_ERR_INVALID_ARG;
    }
    char file_path[200] = {};
    sprintf(file_path, "%s/%s", FILE_BASEPATH, key);
    strcpy(file_path, FILE_BASEPATH "/");
    strcat(file_path, key);

    // ESP_LOGI(TAG, "read file:%s, len:%d", file_path, (uint32_t)*length);
    FILE* fptr = fopen(file_path, "rb");
    if (NULL == fptr) {
        *length = 0;
        // ESP_LOGW(TAG, "cannot open file");
        return ESP_ERR_NOT_FOUND;
    }

    int read_len = *length; // ?
    read_len = fread(buffer, 1, (size_t)read_len, fptr);
    fclose(fptr);
    if (read_len <= 0) {
        *length = 0;
        // ESP_LOGE(TAG, "read error %d", read_len);
        return ESP_ERR_NOT_FOUND;
    }

    *length = read_len;
    return ESP_OK;
}