#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp8266/gpio_struct.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "censtar_8_digit.h"




esp_err_t display_censtar_8_digit_init(xQueueHandle *send_queue)
{
    esp_err_t ret = ESP_OK;

    return ret;
}