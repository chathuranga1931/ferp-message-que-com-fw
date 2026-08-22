// app_device_info.h
//
// Application device identity structure, keys, and permission tables.
//
// Device info is RUNTIME-ONLY identity data: it is never stored to flash,
// has no compile-time defaults, and becomes valid only after the relevant
// external system (e.g. cloud) provides it.
//
// Adding a new field:
//   1. Add the variable to app_device_info_t.
//   2. Define a DEV_INFO_KEY_* constant in the key section below.
//   3. Add a permission table if a new writer set is needed.
//   4. Add the entry to k_dev_info_table[] in app_device_info.cpp.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hsys_device_info.h"

// ---------------------------------------------------------------------------
// Runtime identity data
// ---------------------------------------------------------------------------

typedef struct {
    char     device_uuid[40];        ///< Cloud-assigned UUID (set during onboarding)
    char     device_group[32];       ///< Group/fleet tag — hardcoded default set in app.cpp before startup
    char     hw_address[13];         ///< eFuse MAC, 12 lowercase hex chars + null; set by ModuleDeviceInfo::init()
    char     fw_version[24];         ///< Firmware version string — set at startup from APP_FW_VERSION #define
    char     hw_version[16];         ///< Hardware/PCB version string — set at startup from APP_HW_VERSION #define
    char     disp_tap_version[24];   ///< Display-tap firmware version — written by OTA module after a display-tap update
} app_device_info_t;

// ---------------------------------------------------------------------------
// Device info keys — 16-bit, section 0xA000–0xAFFF
//
//   0xA0xx  Device identity (cloud-assigned)
//   0xA1xx  Hardware identity (OTP / eFuse — reserved for future use)
// ---------------------------------------------------------------------------

// Cloud-assigned identity
#define DEV_INFO_KEY_DEVICE_UUID        0xA001u
#define DEV_INFO_KEY_DEVICE_GROUP       0xA002u

// Hardware identity (read-only — set from eFuse at boot, no message-bus writers)
#define DEV_INFO_KEY_HW_ADDRESS         0xA003u

// Compile-time version identity (read-only — pre-populated from #defines at startup)
#define DEV_INFO_KEY_FW_VERSION         0xA004u
#define DEV_INFO_KEY_HW_VERSION         0xA005u

// Runtime version — updated by the OTA module when the display-tap firmware is flashed
#define DEV_INFO_KEY_DISP_TAP_VERSION   0xA006u

// ---------------------------------------------------------------------------
// Write-permission tables
//
// Each table is a const array of module IDs that are permitted to write a
// given field.  Pass as write_perm + write_perm_count in the info table.
// Tables can be shared by multiple entries that have the same writer set.
// ---------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

/** Permission table: only the cloud module may write these fields. */
extern const hsys_module_id_t k_dev_info_perm_cloud_write[];
extern const uint8_t          k_dev_info_perm_cloud_write_count;

/** Permission table: only the OTA module may write the display-tap version field. */
extern const hsys_module_id_t k_dev_info_perm_ota_write[];
extern const uint8_t          k_dev_info_perm_ota_write_count;

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

/** Returns pointer to the live app_device_info_t instance. */
app_device_info_t   *app_device_info_get(void);

/** Returns the device info table and its size. */
dev_info_entry_t    *app_device_info_get_table(uint16_t *out_count);

#ifdef __cplusplus
}
#endif
