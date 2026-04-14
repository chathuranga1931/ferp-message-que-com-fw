#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include "device.h"
#include "com_esp07.h"
#include "production_test.h"
#include "board_2404.h"
#include "serial_flasher.h"
#include "cmd_esp07.h"
#include "udp_terminal.h"

void power_down_event(void *arg)
{
    Serial.println("Power Down...");
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting Main Board...!");
    SPIFFS.begin();
    // udp_terminal_init();
    initBoard(power_down_event, NULL);
    start_serial_flash(false);

    esp07_get_fw_version(NULL);
    esp07_get_fw_name();
    // esp07_get_fw_timedate();
    // esp07_set_display_type(DIS_NONE);
    // esp07_set_display_type(DIS_CENSTAR_6_DIGIT);
    // esp07_set_display_type(DIS_CENSTAR_7_DIGIT);
    // esp07_set_display_type(DIS_WAYNE_6_DIGIT);
    esp07_set_display_type(DIS_HONGYANG_8_DIGIT);
    // esp07_set_display_type(DIS_SANKI_6_DIGIT);

    // productionTest();
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
        esp07_get_fw_name();
        esp07_set_display_type(DIS_SANKI_6_DIGIT);
    }
}