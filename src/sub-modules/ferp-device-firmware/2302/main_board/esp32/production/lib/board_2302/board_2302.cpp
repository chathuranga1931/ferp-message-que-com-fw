#include <Arduino.h>
#include "board_2302.h"

void(*pwr__dw_cb_evt)(void) = NULL;

void initBoard()
{
    // INPUTS
    pinMode(SWITCH, INPUT_PULLUP);
    pinMode(VIN_LOW, INPUT_PULLUP);
    pinMode(INPUT1, INPUT_PULLUP);
    pinMode(INPUT2, INPUT_PULLUP);
    pinMode(IRQ_RF, INPUT_PULLUP);

    // OUTPUTS
    // pinMode(nRESET_ESP32, OUTPUT);
    pinMode(nRESET_ESP07, OUTPUT);
    pinMode(BUZZER, OUTPUT);
    pinMode(OUTPUT1, OUTPUT);
    pinMode(OUTPUT2, OUTPUT);
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);
    pinMode(LED3, OUTPUT);
    pinMode(EN_4G, OUTPUT);
    pinMode(SPI_CS_SD, OUTPUT);
    pinMode(SPI_CS_RF, OUTPUT);
    pinMode(RESET_RF, OUTPUT);

    
}

void pwrdwn_event()
{
    Serial.println("PWR going down!!!!");
}

void attach_pwrdwn_event(void(*event)(void))
{
    pwr__dw_cb_evt = event;
    attachInterrupt(VIN_LOW, pwrdwn_event, ONLOW);
}