/**
 * @file ota_driver_esp32_dt.h
 * @brief OTA filesystem driver for ESP32 dispTap (esp07) binary files.
 *
 * Writes received firmware chunks to a fixed SPIFFS path using PAL SPIFFS
 * APIs.  No timestamps — the file is always overwritten at the same path
 * so the dispTap update process can find it reliably.
 *
 *   Simulator: <cwd>/SPIFFS/spiffs/<spiffs_path>
 *   ESP32 VFS: /spiffs/<spiffs_path>
 *
 * The driver is generic.  Three separate context instances (one per binary
 * file) point at different fixed paths:
 *
 *   ctx → { .spiffs_path = "esp32/bootloader.bin" }
 *   ctx → { .spiffs_path = "esp32/partitions.bin" }
 *   ctx → { .spiffs_path = "esp32/firmware.bin"   }
 *
 * The images are stored by app_dt_image on the SD card (/dtfw/<file name>);
 * without a mounted card fopen() fails and the OTA is refused.
 *
 * Driver op mapping:
 *   fopen  → app_dt_image_write_begin  (new "<name>.tmp")
 *   fwrite → app_dt_image_write        (append chunk)
 *   fclose → app_dt_image_write_commit (verify size + CRC32, swap in)
 *   ferase → app_dt_image_write_abort  (remove partial file)
 *   fread  → not supported
 */

#pragma once

#include "FileSystemDriver.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context for one dispTap OTA target.
 *
 * Allocate one per binary file with static storage duration in app.cpp and
 * initialise `.spiffs_path` to the desired relative SPIFFS path.
 */
typedef struct {
    const char *spiffs_path; ///< Fixed SPIFFS-relative path, e.g. "esp32/bootloader.bin"
    bool        is_open;     ///< True between fopen() and fclose()/ferase()
} ota_esp32_dt_ctx_t;

/**
 * @brief Single shared driver table for all dispTap OTA targets.
 * Pass a pointer to this as ota_target_desc_t.driver.
 */
extern const ota_fs_driver_t g_ota_driver_esp32_dt;

#ifdef __cplusplus
}
#endif
