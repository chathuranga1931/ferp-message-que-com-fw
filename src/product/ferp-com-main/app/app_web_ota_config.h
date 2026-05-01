// app_web_ota_config.h
//
// Wires ModuleWebClientOta with the cloud-polling OTA target list.
//
// Each web_ota_target_cfg_t entry maps a target_idx (matching
// k_ota_targets[] in app_ota_config.h) to the firmware_type string the
// server expects and the currently running version.
//
// current_version is set at compile time from FW_VERSION (version.h).
// For dispTap targets the version is unknown at build time so "0.0.0" is
// used as a sentinel — the server will always report an available update
// until an actual version is recorded via config.
//
// This header is included ONLY from app.cpp (it defines static objects).

#pragma once

#include "ModuleWebClientOta.h"
#include "app_module_ids.h"

#ifndef FW_VERSION
#  define FW_VERSION  "0.0.0"
#endif

// ── Target poll list (one entry per target that should be cloud-monitored) ──

static const web_ota_target_cfg_t k_web_ota_targets[] = {
    // target_idx   firmware_type           current_version
    { 0,            "ferp-esp32-main",      FW_VERSION  },   ///< Main ESP32 firmware
    { 1,            "ferp-esp32-dt-boot",   "0.0.0"     },   ///< dispTap bootloader
    { 2,            "ferp-esp32-dt-part",   "0.0.0"     },   ///< dispTap partitions
    { 3,            "ferp-esp32-dt-fw",     "0.0.0"     },   ///< dispTap application
};

#define WEB_OTA_TARGET_COUNT  (sizeof(k_web_ota_targets) / sizeof(k_web_ota_targets[0]))

// ── Module singleton ────────────────────────────────────────────────────────

static ModuleWebClientOta s_web_client_ota_module(
    k_web_ota_targets,
    (uint8_t)WEB_OTA_TARGET_COUNT);
