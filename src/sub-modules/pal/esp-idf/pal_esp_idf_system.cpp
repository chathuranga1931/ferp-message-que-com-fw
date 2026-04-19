/**
 * @file pal_esp_idf_system.cpp
 * @brief ESP-IDF implementation of pal_system_init() — intentionally empty.
 *
 * On the ESP32 target the IDF bootloader, app_main(), and FreeRTOS are
 * already fully initialised before our app_init() runs.  There is nothing
 * extra to do here.
 */
#include "pal_system.h"

extern "C" void pal_system_init(void)
{
    /* nothing — ESP-IDF handles all system-level init */
}
