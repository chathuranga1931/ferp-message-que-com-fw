/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "spiff_mount.h"
#include "device.h"
#include "esp_log.h"
#include "cJSON.h"
#include "sys_types.h"
#include "settings.h"
#include "driver/uart.h"

#include "board_io.h"
#include "driver/gpio.h"
#include "production_io.h"

#include "censtar_7cs_digit.h"
static const char *TAG = "main";

void task_log(void *arg)
{
    esp_err_t ret = production_io_init();
    int count = 0;
    LOG_PRINT("production:%d", ret);
    while (1)
    {
        // gpio_set_level(D1_OUT_CS, true);
        // vTaskDelay(1);
        // LOG_PRINT("cs out 1:%d, 2:%d\r\n", gpio_get_level(D1_IN_CS), gpio_get_level(D2_IN_CS));

        // gpio_set_level(D1_OUT_CS, false);
        // vTaskDelay(1);
        // LOG_PRINT("cs out 1:%d, 2:%d\r\n", gpio_get_level(D1_IN_CS), gpio_get_level(D2_IN_CS));

        // gpio_set_level(D2_OUT_CS, true);
        // vTaskDelay(1);
        // LOG_PRINT("cs out 1:%d, 2:%d\r\n", gpio_get_level(D1_IN_CS), gpio_get_level(D2_IN_CS));

        // gpio_set_level(D2_OUT_CS, false);
        // vTaskDelay(1);
        // LOG_PRINT("cs out 1:%d, 2:%d\r\n\r\n", gpio_get_level(D1_IN_CS), gpio_get_level(D2_IN_CS));
        // vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGW(__func__, "test task:%d\r\n", count++);
        vTaskDelay(pdMS_TO_TICKS(2*1000));
    }
    vTaskDelete(NULL);
}

int esp_apptrace_vprintf(const char *format, va_list args)
{
    static char log_buffer[256];
    int written = vsnprintf(log_buffer, sizeof(log_buffer), format, args);
    if (written < 0)
    {
        return written;
    }

    size_t tx_len = (size_t)written;
    if (tx_len >= sizeof(log_buffer))
    {
        tx_len = sizeof(log_buffer) - 1;
        log_buffer[tx_len] = '\0';
    }

    uart_write_bytes(UART_NUM_2, log_buffer, tx_len);
    return written;
}

void app_main(void)
{
    esp_err_t ret = ESP_OK;

    // if (uart_is_driver_installed(UART_NUM_0))
    //     uart_driver_delete(UART_NUM_0);

    // const uart_config_t uart_config = {
    //     .baud_rate = 115200,
    //     .data_bits = UART_DATA_8_BITS,
    //     .parity = UART_PARITY_DISABLE,
    //     .stop_bits = UART_STOP_BITS_1,
    //     .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    //     // .source_clk = UART_SCLK_APB,
    // };
    // // uart_set_mode(UART_NUM_2, UART_MODE_UART);
    // uart_param_config(UART_NUM_2, &uart_config);
    // uart_set_pin(UART_NUM_2, 17, 16, -1, -1);
    // uart_driver_install(UART_NUM_2, 512, 512, 0, NULL, 0);
    // esp_log_set_vprintf(esp_apptrace_vprintf);

    /* cJSON init */
    cJSON_Hooks hooks = {
        .malloc_fn = malloc,
        .free_fn = free};
    cJSON_InitHooks(&hooks);

	ESP_ERROR_GOTO(ret, restart, mount_file_system()); // mount file system

    init_system_settings();

    // settings.display = DIS_LONGFENG_8_DIGIT;

	ESP_ERROR_GOTO(ret, restart, device_init());

    ESP_LOGI(TAG, "Starting %s, heap:%ldKB", PROJECT_NAME, esp_get_free_heap_size()/1024);
    // xTaskCreate(task_log, "task_log", 4*1024, NULL, 10, NULL);

    return;

restart:
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
