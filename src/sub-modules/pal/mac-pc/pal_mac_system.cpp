/**
 * @file pal_mac_system.cpp
 * @brief macOS implementation of pal_system_init().
 *
 * Called by app_init() as the very first operation.  Responsibilities:
 *   1. Start mac_driver — opens the TCP port before any PAL layer tries to
 *      send data, so the UI port is ready from the very first log line.
 *   2. Register ModuleSimBridge as an extra HSYS module + task — kept here
 *      so main.cpp has zero knowledge of simulator-specific modules.
 *   3. Register ModuleMqtt — MQTT is a simulator-only extra (ESP32 handles
 *      MQTT via its own IDF component registration path).
 */
#include "pal_system.h"
#include "pal_power.h"
#include "driver/mac_driver.h"
#include "driver/module_sim_bridge.h"
#include "ModuleMqtt.h"
#include "app.h"
#include <cstdlib>

#ifndef MAC_DRIVER_TCP_PORT
#define MAC_DRIVER_TCP_PORT 9000
#endif

extern "C" void pal_system_init(void)
{
    // 1. Start the TCP driver — must be first so UI port is open before any
    //    PAL file (e.g. pal_mac_gpio) tries to call mac_driver_send_*().
    mac_driver_init(MAC_DRIVER_TCP_PORT);

    // 2. Register the sim bridge module so it appears in the HSYS module/task
    //    tables built by app_init() without main.cpp needing to know about it.
    static const hsys_task_desc_t sim_bridge_task = {
        "sim_brdg_t", 4096, 2, 0, { SIM_BRIDGE_MODULE_ID, 0 }
    };
    app_register_extra_module(ModuleSimBridge::instance(), &sim_bridge_task);

    // 3. Register the MQTT broker client module.
    //    Bridges the HSYS message bus to the configured MQTT broker.
    static const hsys_task_desc_t mqtt_task = {
        "mqtt_task", 8192, 4, 0, { MODULE_MQTT_ID, 0 }
    };
    app_register_extra_module(ModuleMqtt::instance(), &mqtt_task);
}

extern "C" void pal_power_reset(void)
{
    // Simulator: OTA "reboot" — gracefully exit; a test harness can restart
    // the process with the newly-written ota_download.bin if needed.
    std::exit(0);
}
