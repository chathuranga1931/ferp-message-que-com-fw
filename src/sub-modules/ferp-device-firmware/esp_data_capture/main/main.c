/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "longfeng_capture.h"
#include "censtarcs_capture.h"
#include "hongyang_8_digit.h"

#define LONGFENG_DISPLAY 1
// #define CENSTARCS_DISPLAY 1

void app_main(void)
{
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(esp_timer_early_init());

    init_hongyang_display();

#if LONGFENG_DISPLAY
    init_longfeng_capture();
#elif CENSTARCS_DISPLAY
    init_censtarcs_capture();
#else
    #error "No Display Selected"
#endif

    printf("\r\nsystem initialized...\r\nApp:" PROJECT_NAME " FW:" PROJECT_VER " Time:" PROJECT_TIME " Date:" PROJECT_DATE "\r\n\r\n");
}


