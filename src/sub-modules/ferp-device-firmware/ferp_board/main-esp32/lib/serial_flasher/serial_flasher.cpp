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

#include "flasher_configs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/param.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_ota_ops.h>
#include "flasher_configs.h"
#include "esp32_port.h"
#include "esp_loader_io.h"
#include "serial_flasher.h"
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include "cmd_distap.h"
#include "board.h"
#include "com_distap.h"

#if defined(DISTAP_ESP07)
#define FIRMWARE_BASE_PATH "/spiffs/esp07/"
#define BOOTLOADER_NAME "bootloader.bin"
#define BOOTLOADER_ADDRESS 0x00
#define APP_NAME "rtos_dis_tap_esp07.bin"
#define APP_ADDRESS 0x10000
#define PARTITION_TABLE "partitions_table.bin"
#define PARTITION_ADDRESS 0x8000
#elif defined(DISTAP_ESP32)
#define FIRMWARE_BASE_PATH "/spiffs/esp32/"
#define BOOTLOADER_NAME "bootloader.bin"
#define BOOTLOADER_ADDRESS 0x1000
#define PARTITION_TABLE "partition_table.bin"
#define PARTITION_ADDRESS 0x8000
#define APP_NAME "distap_esp32.bin"
#define APP_ADDRESS 0x10000
#else
#error "No Display Tap defined!"
#endif

esp_err_t read_app_info(const char *file_name, esp_app_desc_t *app_desc);

void start_serial_flash(bool skip_version_check)
{
    example_binaries_t bin = {
        .boot = {
            .data = nullptr,
            .file_name = FIRMWARE_BASE_PATH BOOTLOADER_NAME,
            .size = 0,
            .addr = BOOTLOADER_ADDRESS,
        },
        .part = {
            .data = nullptr,
            .file_name = FIRMWARE_BASE_PATH PARTITION_TABLE,
            .size = 0,
            .addr = PARTITION_ADDRESS,
        },
        .app = {
            .data = nullptr,
            .file_name = FIRMWARE_BASE_PATH APP_NAME,
            .size = 0,
            .addr = APP_ADDRESS,
        }};
    esp_app_desc_t app_desc = {};
    char version[sizeof(esp_app_desc_t::version)] = {};
    esp_err_t err1 = read_app_info(bin.app.file_name, &app_desc);
    if (err1 != ESP_OK)
    {
        printf("app verification failed.\r\n");
        return;
    }
    printf("Display Tap App name:%s, Ver:%s\r\n", app_desc.project_name, app_desc.version);
    err1 = distap_get_fw_version(version);
    if (err1 == ESP_OK)
    {
        if (!skip_version_check && strcmp(app_desc.version, version) == 0)
        {
            printf("Display Tap App Version is matching! No firmware update\r\n");
            return;
        }
    }
    else
    {
        printf("Display Tap firmware version read Failed.\r\n");
    }
    suspend_comms_distap();

    printf("Starting Write Firmware...\r\n");

    gpio_set_mode_output_io0_distap();
    const loader_esp32_config_t config = {
        .uart_port = UART_NUM_2,
        .reset_trg_fn = gpio_set_reset_distap,
        .gpio0_trg_fn = gpio_set_io0_distap};

    if (loader_port_esp32_init(&config) != ESP_LOADER_SUCCESS)
    {
        printf("serial initialization failed.\r\n");
        return;
    }
    printf("Connectig to target...\r\n");
    esp_loader_error_t err = connect_to_target(HIGHER_BAUDRATE);
    if (err == ESP_LOADER_SUCCESS)
    {
        target_chip_t chip = esp_loader_get_target();
        printf("Chip:%s Loading bootloader...\r\n", esp_loader_get_target_str(chip));
        if (flash_binary(bin.boot.file_name, bin.boot.size, bin.boot.addr) != ESP_LOADER_SUCCESS)
        {
            printf("Chip:%s Loading bootloader failed\r\n", esp_loader_get_target_str(chip));
        }
        printf("Loading partition table...\r\n");
        if (flash_binary(bin.part.file_name, bin.part.size, bin.part.addr) != ESP_LOADER_SUCCESS)
        {
            printf("Loading partition table Failed!\r\n");
        }
        printf("Loading app...");
        if (flash_binary(bin.app.file_name, bin.app.size, bin.app.addr) != ESP_LOADER_SUCCESS)
        {
            printf("Loading app Failed!");
        }
        printf("Done!\r\n");
    }
    else
    {
        printf("Conecting to target failed!\r\n");
    }

    gpio_reset_io0_distap();
    loader_port_reset_target();
    board_delay_ms(500);
    resume_comms_distap();
}

esp_err_t read_app_info(const char *file_name, esp_app_desc_t *app_desc)
{
    esp_err_t err = ESP_OK;
    FILE *fptr = NULL;
    size_t file_size;
    if (file_name == NULL || app_desc == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    fptr = fopen(file_name, "rb");
    if (fptr == NULL)
    {
        printf("%s not found\r\n", file_name);
        return ESP_ERR_NOT_FOUND;
    }
    fseek(fptr, 0, SEEK_END);
    file_size = ftell(fptr);
    fseek(fptr, 0, SEEK_SET); // rewind to zero position, same as rewind(fptr);
    size_t lenght = 0;
    while (lenght < file_size)
    {
        fread(&app_desc->magic_word, sizeof(esp_app_desc_t::magic_word), 1, fptr);
        lenght += 4;
        if(app_desc->magic_word == ESP_APP_DESC_MAGIC_WORD)
        {
            printf("Magic word found @0x%x, %dKB\r\n", file_size-lenght, (file_size-lenght)/8);
            fread(&app_desc->secure_version, sizeof(esp_app_desc_t) - sizeof(esp_app_desc_t::magic_word), 1, fptr);
            break;
        }
    }
    if(lenght >= file_size)
    {
        printf("Magic word not found\r\n");
        err = ESP_ERR_NOT_SUPPORTED;
        goto end;
    }
end:
    fseek(fptr, 0, SEEK_SET); // rewind(fptr);
    fclose(fptr);
    return err;
}

esp_loader_error_t connect_to_target(uint32_t higher_transmission_rate)
{
    esp_loader_connect_args_t connect_config = ESP_LOADER_CONNECT_DEFAULT();

    esp_loader_error_t err = esp_loader_connect(&connect_config);
    if (err != ESP_LOADER_SUCCESS)
    {
        printf("Cannot connect to target. Error: %u\n", err);
        return err;
    }
    printf("Connected to target\n");

#ifdef SERIAL_FLASHER_INTERFACE_UART
    const target_chip_t chip = esp_loader_get_target();
    if (higher_transmission_rate &&  chip != ESP8266_CHIP)
    {
        err = esp_loader_change_transmission_rate(higher_transmission_rate);
        if (err == ESP_LOADER_ERROR_UNSUPPORTED_FUNC)
        {
            printf("%s does not support change transmission rate command.", esp_loader_get_target_str(chip));
            return err;
        }
        else if (err != ESP_LOADER_SUCCESS)
        {
            printf("Unable to change transmission rate on target.");
            return err;
        }
        else
        {
            err = loader_port_change_transmission_rate(higher_transmission_rate);
            if (err != ESP_LOADER_SUCCESS)
            {
                printf("Unable to change transmission rate.");
                return err;
            }
            printf("Transmission rate changed changed\n");
        }
    }
#endif /* SERIAL_FLASHER_INTERFACE_UART */

    return ESP_LOADER_SUCCESS;
}

#ifdef SERIAL_FLASHER_INTERFACE_UART
esp_loader_error_t flash_binary(const char *file_name, size_t size, size_t address)
{
    esp_loader_error_t err = ESP_LOADER_SUCCESS;
    static uint8_t payload[1024];
    size_t binary_size = 0;
    size_t written = 0;
    FILE *fptr = NULL;

    if (file_name == NULL)
    {
        printf("Invalid parameter\r\n");
        return ESP_LOADER_ERROR_INVALID_PARAM;
    }
    fptr = fopen(file_name, "rb");
    if (fptr == NULL)
    {
        printf("%s not found\r\n", file_name);
        return ESP_LOADER_ERROR_FAIL;
    }
    fseek(fptr, 0, SEEK_END);
    size = ftell(fptr);
    fseek(fptr, 0, SEEK_SET); // same as rewind(fptr);

    printf("Erasing flash (this may take a while)...\r\n");
    err = esp_loader_flash_start(address, size, sizeof(payload));
    if (err != ESP_LOADER_SUCCESS)
    {
        printf("Erasing flash failed with error %d.\r\n", err);
        goto end;
    }
    printf("Start programming\r\n");

    binary_size = size;
    written = 0;

    while (size > 0)
    {
        size_t to_read = MIN(size, sizeof(payload));
        // memcpy(payload, bin_addr, to_read);
        fread(payload, to_read, 1, fptr);

        err = esp_loader_flash_write(payload, to_read);
        if (err != ESP_LOADER_SUCCESS)
        {
            printf("\r\nPacket could not be written! Error %d.\r\n", err);
            goto end;
        }

        size -= to_read;
        // bin_addr += to_read; //skip this because file pointer is already doing it
        written += to_read;

        int progress = (int)(((float)written / binary_size) * 100);
        printf("\rProgress: %d %%", progress);
        fflush(stdout);
    };

    printf("\r\nFinished programming\r\n");

#if MD5_ENABLED
    err = esp_loader_flash_verify();
    if (err == ESP_LOADER_ERROR_UNSUPPORTED_FUNC)
    {
        printf("ESP8266 does not support flash verify command.\r\n");
        goto end;
    }
    else if (err != ESP_LOADER_SUCCESS)
    {
        printf("MD5 does not match. err: %d\r\n", err);
        goto end;
    }
    printf("Flash verified\r\n");
#endif

end:
    fseek(fptr, 0, SEEK_SET); // rewind(fptr);
    fclose(fptr);
    return err;
}
#endif /* SERIAL_FLASHER_INTERFACE_UART */

esp_loader_error_t load_ram_binary(const uint8_t *bin)
{
    printf("Start loading\r\n");
    esp_loader_error_t err;
    const esp_loader_bin_header_t *header = (const esp_loader_bin_header_t *)bin;
    esp_loader_bin_segment_t segments[header->segments];

    // Parse segments
    uint32_t seg;
    uint32_t *cur_seg_pos;
    for (seg = 0, cur_seg_pos = (uint32_t *)(&bin[BIN_FIRST_SEGMENT_OFFSET]);
         seg < header->segments;
         seg++)
    {
        segments[seg].addr = *cur_seg_pos++;
        segments[seg].size = *cur_seg_pos++;
        segments[seg].data = (uint8_t *)cur_seg_pos;
        cur_seg_pos += (segments[seg].size) / 4;
    }

    // Download segments
    for (seg = 0; seg < header->segments; seg++)
    {
        printf("Downloading %" PRIu32 " bytes at 0x%08" PRIx32 "...\r\n", segments[seg].size, segments[seg].addr);

        err = esp_loader_mem_start(segments[seg].addr, segments[seg].size, ESP_RAM_BLOCK);
        if (err != ESP_LOADER_SUCCESS)
        {
            printf("Loading ram start with error %d.\r\n", err);
            return err;
        }

        size_t remain_size = segments[seg].size;
        uint8_t *data_pos = segments[seg].data;
        while (remain_size > 0)
        {
            size_t data_size = MIN(ESP_RAM_BLOCK, remain_size);
            err = esp_loader_mem_write(data_pos, data_size);
            if (err != ESP_LOADER_SUCCESS)
            {
                printf("\nPacket could not be written! Error %d.\r\n", err);
                return err;
            }
            data_pos += data_size;
            remain_size -= data_size;
        }
    }

    err = esp_loader_mem_finish(header->entrypoint);
    if (err != ESP_LOADER_SUCCESS)
    {
        printf("\nLoad ram finish with Error %d.\r\n", err);
        return err;
    }
    printf("\r\nFinished loading\r\n");

    return ESP_LOADER_SUCCESS;
}
