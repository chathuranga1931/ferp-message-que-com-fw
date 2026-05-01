
#pragma once


// ── OTA platform configuration ─────────────────────────────────────────────
// ota_platform_get_config() wires the source/target tables for OtaModule.
// The target driver (ota_driver_esp32_main) is platform-agnostic: it calls
// pal_fw_update_* which maps to ESP-IDF OTA partitions on device and to a
// host-file stream (<cwd>/ota_download.bin) on the simulator.
// OtaModule stores only pointers — static storage duration required.
#include "ota_driver_esp32_main.h"
#include "ota_driver_esp32_dt.h"
#include "app_module_ids.h"

#ifndef MODULE_MQTT_ID
#define MODULE_MQTT_ID  ((hsys_module_id_t)0xFF)   // placeholder — not yet implemented
#endif

static const ota_source_desc_t k_ota_sources[] = {
    // source_module_id             priority  _pad  timeout_ms
    { MODULE_MQTT_ID,              0,        0,    60000  },
    { MODULE_WEB_SERVER_ID,        0,        0,    120000 },  ///< simulator web OTA (port 8080)
    { MODULE_WEB_CLIENT_OTA_ID,    0,        0,    120000 },  ///< cloud-polling OTA source
};

static ota_esp32_ctx_t    s_esp32_ota_ctx    = {};

/* dispTap binary files — one context per file, each holds the fixed SPIFFS path */
static ota_esp32_dt_ctx_t s_esp32_dt_boot_ctx = { .spiffs_path = "esp32/bootloader.bin",  .is_open = false };
static ota_esp32_dt_ctx_t s_esp32_dt_part_ctx = { .spiffs_path = "esp32/partitions.bin",  .is_open = false };
static ota_esp32_dt_ctx_t s_esp32_dt_fw_ctx   = { .spiffs_path = "esp32/firmware.bin",    .is_open = false };

static const ota_target_desc_t k_ota_targets[] = {
    {
        .target_idx   = 0,
        .needs_reboot = true,
        ._pad         = {},
        .label        = "esp32-main",
        .driver       = &g_ota_driver_esp32_main,
        .ctx          = &s_esp32_ota_ctx,
    },
    {
        .target_idx   = 1,
        .needs_reboot = false,   ///< dispTap binary staged to SPIFFS — no CPU reset needed
        ._pad         = {},
        .label        = "esp32-dt-boot",
        .driver       = &g_ota_driver_esp32_dt,
        .ctx          = &s_esp32_dt_boot_ctx,
    },
    {
        .target_idx   = 2,
        .needs_reboot = false,   ///< dispTap binary staged to SPIFFS — no CPU reset needed
        ._pad         = {},
        .label        = "esp32-dt-part",
        .driver       = &g_ota_driver_esp32_dt,
        .ctx          = &s_esp32_dt_part_ctx,
    },
    {
        .target_idx   = 3,
        .needs_reboot = false,   ///< dispTap binary staged to SPIFFS — no CPU reset needed
        ._pad         = {},
        .label        = "esp32-dt-fw",
        .driver       = &g_ota_driver_esp32_dt,
        .ctx          = &s_esp32_dt_fw_ctx,
    },
};

#define OTA_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

extern "C" void ota_platform_get_config(
    const ota_source_desc_t **sources, uint8_t *source_count,
    const ota_target_desc_t **targets, uint8_t *target_count)
{
    *sources      = k_ota_sources;
    *source_count = (uint8_t)OTA_ARRAY_SIZE(k_ota_sources);
    *targets      = k_ota_targets;
    *target_count = (uint8_t)OTA_ARRAY_SIZE(k_ota_targets);
}
