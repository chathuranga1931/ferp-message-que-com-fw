/**
 * @file pal_esp_idf_system.cpp
 * @brief ESP-IDF implementation of pal_system_init().
 *
 * All HSYS modules (including ModuleWebServer) are registered in the
 * shared app.cpp module table.  There are no ESP32-only extras.
 */
#include "pal_system.h"
#include "pal_power.h"
#include "esp_system.h"

extern "C" void pal_system_init(void)
{
    /* nothing — all modules registered in the shared app.cpp table */
}

extern "C" void pal_power_reset(void)
{
    esp_restart();
}

