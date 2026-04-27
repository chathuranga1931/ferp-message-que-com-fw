/**
 * @file ota_driver_esp32_main.h
 * @brief OTA filesystem driver for the ESP32 main application firmware slot.
 */

#pragma once

#include "FileSystemDriver.h"
#include "pal_fw_update.h"   /* pal_fw_update_handle_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque context for the ESP32 main OTA driver.
 * Allocate one instance with static storage in main.cpp; zero-initialise it.
 */
typedef struct {
    pal_fw_update_handle_t handle; ///< Active PAL firmware-update session handle; NULL = no session
} ota_esp32_ctx_t;

/**
 * @brief Global driver table for the ESP32 main OTA target.
 * Pass a pointer to this as ota_target_desc_t.driver.
 */
extern const ota_fs_driver_t g_ota_driver_esp32_main;

#ifdef __cplusplus
}
#endif
