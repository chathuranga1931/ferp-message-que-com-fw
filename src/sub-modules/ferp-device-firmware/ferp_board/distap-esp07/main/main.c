/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include "esp_log.h"
#include "cJSON.h"
#include "spiff_mount.h"
#include "device.h"
#include "settings.h"
// #include "esp_spi_flash.h"

#define ESP_ERROR_RESTART(x)      \
    {                             \
        esp_err_t err_rc_ = (x);  \
        if ((err_rc_ != ESP_OK))  \
        {                         \
            goto restart_onfault; \
        }                         \
    }

static const char *TAG = "main";

void app_main()
{
	
    // ESP_ERROR_RESTART(esp_event_loop_create_default());
    /* cJSON init */
    cJSON_Hooks hooks = {
        .malloc_fn = malloc,
        .free_fn = free};
    cJSON_InitHooks(&hooks);

	ESP_ERROR_RESTART(mount_file_system()); // mount file system
    
    init_system_settings();

    /*Display Debug*/
    // load_default_system_settings();
    // settings.display = DIS_HONGYANG_8_DIGIT;
    // settings.display = DIS_WAYNE_6_DIGIT;
    // settings.display = DIS_CENSTAR_6_DIGIT;
    // settings.display = DIS_CENSTAR_7_DIGIT;
    // settings.display = DIS_NONE;
    // settings.display = DIS_SANKI_6_DIGIT;
    // settings.display = DIS_LONGFENG_8_DIGIT;
    // settings.error_mask.err_bit.price_gap = false;
    // save_system_settings();

	ESP_ERROR_RESTART(device_init());
    ESP_LOGI(TAG, "Starting %s, heap:%dKB", PROJECT_NAME, esp_get_free_heap_size()/1024);
	return;
restart_onfault:
    ESP_LOGE(TAG, "Fault on Start");
    vTaskDelay(pdMS_TO_TICKS(500)); // wait until 1 seconds
    esp_restart();
}
