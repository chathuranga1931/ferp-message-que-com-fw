#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include "device.h"
#include "com_distap.h"
#include "production_test.h"
#include "board.h"
#include "serial_flasher.h"
#include "cmd_distap.h"
#include "udp_terminal.h"

void power_down_event(void *arg)
{
    Serial.println("Power Down...");
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\r\n\r\nStarting Main Board...!");
    SPIFFS.begin();
    // udp_terminal_init();
    initBoard(power_down_event, NULL);
    start_serial_flash(false);

    distap_get_fw_version(NULL);
    distap_get_fw_name();
    distap_get_fw_timedate();
    // // distap_set_display_type(DIS_NONE);
    // // distap_set_display_type(DIS_CENSTAR_6_DIGIT);
    // // distap_set_display_type(DIS_CENSTAR_7_DIGIT);
    // // distap_set_display_type(DIS_WAYNE_6_DIGIT);
    // distap_set_display_type(DIS_CENSTAR_7_DIGIT_CS);
    // distap_set_display_type(DIS_LONGFENG_8_DIGIT);
    // // distap_set_display_type(DIS_SANKI_6_DIGIT);

    // Set the shared LED power OFF, reconfigured as output
    gpio_set_mode_output_io0_distap();
    gpio_set_io0_distap(true);

    productionTest();

    // // board_init();
    // // gpio_set_mode_output_io0_distap();
    // // gpio_set_io0_distap(true);
    // gpio_reset_pin(IO0_DISTAP);
    // gpio_reset_pin(RESET_DISTAP);
    // gpio_set_direction(RESET_DISTAP, GPIO_MODE_OUTPUT);
    // gpio_set_reset_distap(true);
    // delay(200);
    // Serial.println("Down");
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
        // distap_get_fw_name();
        // distap_set_display_type(DIS_SANKI_6_DIGIT);
    }
}