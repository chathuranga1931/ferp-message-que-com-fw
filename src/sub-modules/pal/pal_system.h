/**
 * @file pal_system.h
 * @brief PAL system initialisation — platform-specific boot sequence.
 *
 * pal_system_init() is called by app_init() as the very first operation,
 * before any framework (pool, modules, tasks) is started.
 *
 * Platform responsibilities:
 *   ESP-IDF : no-op (IDF handles all system init before app_main).
 *   macOS   : starts the TCP driver (mac_driver) so the Python UI port is
 *             open before any module calls pal_gpio_set_level() or
 *             mac_driver_send_json().
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Perform all platform-level initialisation required before the application
 * framework starts.  Must be called once, at the very beginning of app_init().
 */
void pal_system_init(void);

#ifdef __cplusplus
}
#endif
