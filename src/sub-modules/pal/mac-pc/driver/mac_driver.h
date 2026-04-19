/**
 * @file mac_driver.h
 * @brief macOS simulator hardware driver — owns ALL communication with the Python UI.
 *
 * This is the single place where the TCP connection to sim_ui.py lives.
 * Every other mac-pc PAL file that needs to exchange data with the UI calls
 * functions declared here instead of managing its own sockets.
 *
 * Callers:
 *   pal_mac_system.cpp   — mac_driver_init()          on startup
 *   pal_mac_gpio.cpp     — mac_driver_send_gpio()     on every output level change
 *   module_sim_bridge    — mac_driver_send_json()     for HSYS message serialisation
 *
 * Data flow:
 *
 *   C++ → Python  (output):
 *     mac_driver_send_json("SIM_GPIO_OUT", "{\"pin\":5,\"level\":1,\"name\":\"LED1\"}")
 *     mac_driver_send_json("MSG_TICK_1000MS", "{}")
 *
 *   Python → C++  (input):
 *     {"id":"SIM_BTN","data":{"btn":"default","action":"press"}}
 *       → mac_driver_read_loop() parses btn name, maps to GPIO pin,
 *         calls pal_gpio_sim_inject_input(pin, level)
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/**
 * Start the TCP server on @p port.  Spawns the accept + read threads
 * internally.  Must be called once from pal_mac_system.cpp::pal_system_init()
 * before any other mac_driver_* function.
 */
void mac_driver_init(uint16_t port);

/* ── Output helpers — C++ → Python UI ────────────────────────────────────── */

/**
 * Send a generic JSON line to the connected Python UI client.
 * The wire format is one line:
 *   {"id":<id>,"ts":<ms>,"data":<data_json>}\n
 *
 * @param id        Event identifier string, e.g. "MSG_TICK_1000MS"
 * @param data_json A complete JSON object string for the payload, e.g. "{}"
 *
 * Thread-safe.  Silently drops the message if no client is connected.
 */
void mac_driver_send_json(const char *id, const char *data_json);

/**
 * Convenience wrapper: send a GPIO output level change to the UI.
 * Formats and calls mac_driver_send_json() with id = "SIM_GPIO_OUT".
 *
 *   {"id":"SIM_GPIO_OUT","ts":<ms>,"data":{"pin":<pin>,"level":<level>,"name":"<name>"}}
 *
 * @param pin   GPIO pin number (e.g. 5 for LED1).
 * @param level 1 = HIGH / on,  0 = LOW / off.
 * @param name  Human-readable name for the pin, e.g. "LED1".  May be NULL.
 */
void mac_driver_send_gpio(int pin, int level, const char *name);

#ifdef __cplusplus
}
#endif
