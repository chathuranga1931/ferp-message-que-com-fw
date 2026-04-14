#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include "device.h"
#include "com_esp07.h"
#include "production_test.h"
#include <Wire.h>
#include "DS1307RTC.h"
#include "serial_flasher.h"
#include "cmd_esp07.h"
#include "udp_terminal.h"

const char *system_get_reset_reason()
{
    const char *resetreason[] = {
        "ESP_RST_UNKNOWN",
        "ESP_RST_POWERON",
        "ESP_RST_EXT",
        "ESP_RST_SW",
        "ESP_RST_PANIC",
        "ESP_RST_INT_WDT",
        "ESP_RST_TASK_WDT",
        "ESP_RST_WDT",
        "ESP_RST_DEEPSLEEP",
        "ESP_RST_BROWNOUT",
        "ESP_RST_SDIO"};

    esp_reset_reason_t rst = esp_reset_reason();
    if (rst <= ESP_RST_SDIO)
        return resetreason[rst];
    else
        return resetreason[0];
}

void power_down_event()
{
    Serial.println("Power Down...");
}

void setup()
{
    SPIFFS.begin();
    printf("Connecting WiFi ");
    // WiFi.mode(WIFI_STA);
    // WiFi.begin(WIFI_SSID, WIFI_PASS);
    // udp_terminal_init();
    // while (WiFi.status() != WL_CONNECTED)
    // {
    //     delay(500);
    //     printf(".");
    // }
    // printf("IP:%s\r\n", WiFi.localIP().toString().c_str());
    initBoard(power_down_event);
    Serial.begin(115200);
    // Wire.begin(I2C_SDA, I2C_SCL, 100000);
    // RTC.begin(&Wire);
    printf("Reset reason:%s\r\n", system_get_reset_reason());
    Serial.println("Starting Main Board...!");

    start_serial_flash();

    esp07_get_fw_version(NULL);
    esp07_get_fw_name();
    esp07_get_fw_timedate();
    esp07_set_display_type(DIS_HONGYANG_8_DIGIT);
    // esp07_set_display_type(DIS_CENSTAR_7_DIGIT);
    // esp07_set_display_type(DIS_WAYNE_6_DIGIT);
    // esp07_set_display_type(DIS_SANKI_6_DIGIT);
    esp07_set_err_mask((const data_error_t){
        .err_bit = {
            .index = true,
            .unitprice = true,
            .totprice = true,
            .volume = true,
            .price_gap = true,
        }
    });
}

void loop()
{
    int count = 0;
    timer_t pre = micros();
    while (1)
    {
        // UDP_PRINT("Testing...%d,  time:%d\r\n", count, micros() - pre);
        // count++;
        // pre =  micros();
        // udp_server_send((uint8_t*)"Testing...\r\n", sizeof("Testing...\r\n") - 1);
        delay(2000);
    }
}