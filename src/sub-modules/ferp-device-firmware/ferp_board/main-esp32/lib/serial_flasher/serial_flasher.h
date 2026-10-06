/* Copyright 2020-2023 Espressif Systems (Shanghai) CO LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "esp_loader.h"

#define BIN_FIRST_SEGMENT_OFFSET    0x18
// Maximum block sized for RAM and Flash writes, respectively.
#define ESP_RAM_BLOCK               0x1800

typedef struct {
    const uint8_t *data;
    const char *file_name;
    uint32_t size;
    uint32_t addr;
} partition_attr_t;

typedef struct {
    partition_attr_t boot;
    partition_attr_t part;
    partition_attr_t app;
} example_binaries_t;

typedef struct {
    partition_attr_t ram_app;
} example_ram_app_binary_t;

/**
 * @brief esptool portable bin header format
 */
typedef struct example_bin_header {
    uint8_t magic;
    uint8_t segments;
    uint8_t flash_mode;
    uint8_t flash_size_freq;
    uint32_t entrypoint;
} example_bin_header_t;

/**
 * @brief esptool portable bin segment format
 */
typedef struct example_bin_segment {
    uint32_t addr;
    uint32_t size;
    uint8_t *data;
} example_bin_segment_t;

/**
 * @brief Optional image source for the flasher.
 *
 * Images are named by file name only ("bootloader.bin", "partition_table.bin",
 * the app image).  open() must succeed before read(); close() is called once
 * for every successful open().  Without registered ops the flasher reads
 * FIRMWARE_BASE_PATH<name> with stdio (SPIFFS).
 */
typedef struct {
    int32_t (*open)(const char *name, size_t *size);                 ///< 0 = OK
    int32_t (*read)(const char *name, size_t offset, uint8_t *buf,
                    size_t len, size_t *bytes_read);                 ///< 0 = OK
    void    (*close)(const char *name);
} serial_flasher_file_ops_t;

/** Register the image source (NULL restores stdio).  The ops must stay valid. */
void serial_flasher_set_file_ops(const serial_flasher_file_ops_t *ops);

esp_loader_error_t connect_to_target(uint32_t higher_transmission_rate);
esp_loader_error_t flash_binary(const char *file_name, size_t size, size_t address);
esp_loader_error_t load_ram_binary(const uint8_t *bin);
void start_serial_flash(bool skip_version_check);
