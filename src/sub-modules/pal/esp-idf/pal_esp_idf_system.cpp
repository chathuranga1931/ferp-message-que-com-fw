/**
 * @file pal_esp_idf_system.cpp
 * @brief ESP-IDF implementation of pal_system_init().
 *
 * Runs before any HSYS task is created (called from app_init() before
 * hsys_task_mgr_init()).  Initialises lwIP and the default event loop here
 * so the tcpip thread exists before any task tries to open a socket —
 * particularly ModuleWebServer::pre_init() which calls httpd_start().
 *
 * All HSYS modules (including ModuleWebServer, ModuleMqtt) are registered in
 * the shared app.cpp module/task tables.  There are no ESP32-only extras.
 */
#include "pal_system.h"
#include "pal_power.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "board.h"   // BSP board_init() — GPIO + UART2 setup (selected by FERP_COM_2602)

extern "C" void pal_system_init(void)
{
    esp_log_level_set("wifi", ESP_LOG_ERROR);

    /* Initialise the TCP/IP stack once, before any task that may open a
     * socket (httpd, MQTT, HTTP client).  The WiFi PAL guards against a
     * second call via its s_netif_initialized flag. */
    esp_netif_init();
    esp_event_loop_create_default();
}

extern "C" void pal_power_reset(void)
{
    esp_restart();
}


