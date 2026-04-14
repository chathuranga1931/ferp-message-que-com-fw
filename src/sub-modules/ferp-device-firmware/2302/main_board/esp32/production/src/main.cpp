#include <Arduino.h>
#include <WiFi.h>
#include "board_2302.h"
#include "production_test.h"
#include "display_tap.h"
#include <Wire.h>
#include "DS1307RTC.h"
// #include <PPPOS.h>
// #include <PPPOSClient.h>

// #define SERIAL_BR 115200
// #define GSM_SERIAL 1
// #define GSM_RX 16
// #define GSM_TX 17
// #define GSM_BR 115200

// const char *server = "example.com";
// const char *ppp_user = "";
// const char *ppp_pass = "";
// const String APN = "internet";

void fuel_event_display_1(display_data_t data)
{
    // Serial.println();
    Serial.println("display 1 = " + String(data.unit_price / 100.0) + ", " + String(data.total_price / 100.0) + ", " + String(data.volume_l / 1000.0) + ", " + (data.flags.start_stop ? "start" : "stop") + ", p=" + (data.flags.select_p ? "1" : "0") + " l=" + (data.flags.select_l ? "1" : "0"));
    //Serial.println("display 1 unit=" + String(data.unit_price / 100.0) + " total=" + String(data.total_price / 100.0) + " volume=" + String(data.volume_l / 1000.0) + " " + (data.flags.start_stop ? "start" : "stop") + " select_p=" + (data.flags.select_p ? "true" : "false") + " select_l=" + (data.flags.select_l ? "true" : "false"));
    // Serial.println();
}
void fuel_event_display_2(display_data_t data)
{
    // Serial.println();
    Serial.println("display 2 = " + String(data.unit_price / 100.0) + ", " + String(data.total_price / 100.0) + ", " + String(data.volume_l / 1000.0) + ", " + (data.flags.start_stop ? "start" : "stop") + ", p=" + (data.flags.select_p ? "1" : "0") + " l=" + (data.flags.select_l ? "1" : "0"));
    // Serial.println("display 2 unit=" + String(data.unit_price / 100.0) + " total=" + String(data.total_price / 100.0) + " volume=" + String(data.volume_l / 1000.0) + " " + (data.flags.start_stop ? "start" : "stop") + " select_p=" + (data.flags.select_p ? "true" : "false") + " select_l=" + (data.flags.select_l ? "true" : "false"));
    // Serial.println();
}
void power_down_event()
{
    Serial.println("Power Down...");
}

void setup()
{
    WiFi.mode(WIFI_OFF);
    // WiFi.begin(WIFI_SSID, WIFI_PASS);
    initBoard();
    attach_pwrdwn_event(power_down_event);
    // digitalWrite(nRESET_ESP07, LOW);
    init_display_tap(fuel_event_display_1, fuel_event_display_2);
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL, 100000);
    RTC.begin(&Wire);
    Serial.println("Starting Main Board...!");

    // digitalWrite(EN_4G, HIGH);
    // Serial.begin(SERIAL_BR);
    // PPPOS_init(GSM_TX, GSM_RX, GSM_BR, GSM_SERIAL, ppp_user, ppp_pass);
}

void loop()
{
    // productionTest();
    while (1)
    {
        display_tap();

        delay(100);
    }
}