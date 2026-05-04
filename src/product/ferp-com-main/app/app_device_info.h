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
    char     device_uuid[40];   ///< Cloud-assigned UUID (set during onboarding)
    char     device_group[32];  ///< Cloud-assigned group / fleet tag
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
