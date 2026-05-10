
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

// ---------------------------------------------------------------------------
// ota_target_find_by_name
// ---------------------------------------------------------------------------

/** Resolve an OTA target name to its target_idx by walking k_ota_targets[].
 *
 *  Accepts:
 *    - Decimal integer string ("0", "1", …)
 *    - Canonical label from k_ota_targets[].label  (e.g. "esp32-main")
 *    - Short alias: label with any leading "esp32-" prefix stripped (e.g. "main")
 *
 *  Returns target_idx on success, 0xFF if not found.
 */
uint8_t ota_target_find_by_name(const char *name)
{
    if (!name || name[0] == '\0') return 0xFF;
    // Accept decimal integer string directly
    char *end;
    long v = strtol(name, &end, 10);
    if (*end == '\0' && end != name && v >= 0 && v < 255) return (uint8_t)v;
    // Walk the table: exact label match first, then short alias (strip "esp32-")
    const uint8_t n = (uint8_t)OTA_ARRAY_SIZE(k_ota_targets);
    for (uint8_t i = 0; i < n; i++) {
        const char *lbl = k_ota_targets[i].label;
        if (!lbl) continue;
        if (strcmp(name, lbl) == 0) return k_ota_targets[i].target_idx;
        if (strncmp(lbl, "esp32-", 6) == 0 && strcmp(name, lbl + 6) == 0)
            return k_ota_targets[i].target_idx;
    }
    return 0xFF;
}

extern "C" void ota_platform_get_config(
    const ota_source_desc_t **sources, uint8_t *source_count,
    const ota_target_desc_t **targets, uint8_t *target_count)
{
    *sources      = k_ota_sources;
    *source_count = (uint8_t)OTA_ARRAY_SIZE(k_ota_sources);
    *targets      = k_ota_targets;
    *target_count = (uint8_t)OTA_ARRAY_SIZE(k_ota_targets);
}
