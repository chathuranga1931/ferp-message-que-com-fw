#include "Arduino.h"
const char c_pin_sdata1 = 3; // PC28
const char c_pin_sdata2 = 2; // PB25
const char c_pin_sclk = 6;   // PC24
const char c_pin_rclk = 4;   // PC26

int count = 0;

void setup()
{
    pinMode(c_pin_sdata1, OUTPUT);
    pinMode(c_pin_sdata2, OUTPUT);
    pinMode(c_pin_sclk, OUTPUT);
    pinMode(c_pin_rclk, OUTPUT);
    Serial.begin(115200);
}

// the loop routine runs over and over again forever:
void loop()
{
    Serial.println("Toggle RCLK");
    for (size_t i = 0; i < 3; i++)
    {
        digitalWrite(c_pin_rclk, true);
        delay(500);
        digitalWrite(c_pin_rclk, false);
        delay(500);
    }
    
    Serial.println("Toggle SCLK");
    for (size_t i = 0; i < 3; i++)
    {
        digitalWrite(c_pin_sclk, true);
        delay(500);
        digitalWrite(c_pin_sclk, false);
        delay(500);
    }

    Serial.println("Toggle DATA1");
    for (size_t i = 0; i < 3; i++)
    {
        digitalWrite(c_pin_sdata1, true);
        delay(500);
        digitalWrite(c_pin_sdata1, false);
        delay(500);
    }
}
