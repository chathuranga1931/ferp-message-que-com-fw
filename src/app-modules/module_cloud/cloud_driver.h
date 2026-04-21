// cloud_driver.h
//
// Abstract cloud driver interface.
//
// ModuleCloud uses this interface exclusively — it has no dependency on any
// specific cloud backend.  Swap in a different backend by providing a
// different cloud_driver_t pointer at init time.
//
// The payload structs here use fixed-size arrays that are independent of any
// backend header.  The cube_sphere wrapper (cube_sphere_cloud_driver.cpp)
// maps between these and the cube_sphere API types.

#pragma once

#include <stdint.h>

// ── Maximum nozzle count supported by the interface ──────────────────────────

#define CLOUD_MAX_NOZZLES   2

// ── Payload structs ───────────────────────────────────────────────────────────

typedef struct {
    int8_t   rssi;
    uint8_t  _pad[3];
    uint32_t uptime_sec;
    uint32_t event_count_success;
    uint32_t event_count_failure;
} cloud_hb_info_t;

typedef struct {
    char    ssid[50];
    char    password[50];
    char    ip_address[25];
    int8_t  rssi;
    uint8_t _pad[3];
    uint32_t uptime_sec;
} cloud_reconnect_info_t;

typedef struct {
    char     ssid[50];
    char     password[50];
    char     ip_address[25];
    char     mac_address_str[25];
    char     fw_version[30];
    char     hw_version[30];
    char     board_version[30];
    char     device_type[30];
    char     esp07_fw_version[30];
    char     sd_card_status[20];
    char     sd_card_size_str[20];
    int8_t   rssi;
    uint8_t  _pad[3];
    uint32_t uptime_sec;
    uint32_t event_count_success;
    uint32_t event_count_failure;
} cloud_startup_info_t;

typedef struct {
    uint8_t  nozzle_idx;
    uint8_t  _pad[3];
    uint64_t time_stamp;
    uint32_t unit_pricex100;
    uint64_t total_pricex100;
    uint64_t volume_lx1000;
    uint32_t event_id;
} cloud_pumped_info_t;

typedef struct {
    char nozzle_uuid[50];    ///< per-nozzle UUID assigned by the cloud
    char nozzle_id[10];
    char fuel_type[10];
    char fuel_type_str[35];
} cloud_nozzle_config_t;

// ── Driver interface ──────────────────────────────────────────────────────────

typedef struct {
    // Provisioning — call once on first boot or after config reset.
    // mac_address_str: 12 hex chars (no separators), e.g. "AABBCCDDEEFF"
    // root_ca: PEM string, or NULL to skip TLS verification
    int32_t (*register_device)(const char *mac_address_str, const char *root_ca);

    // Retrieve per-nozzle config (UUIDs etc.) after successful registration.
    // count: size of the out array (should be CLOUD_MAX_NOZZLES)
    int32_t (*get_nozzle_config)(cloud_nozzle_config_t *out, uint8_t count);

    // Uplink events
    int32_t (*send_startup)        (cloud_startup_info_t info);
    int32_t (*send_heartbeat)      (cloud_hb_info_t info);
    int32_t (*send_reconnect)      (cloud_reconnect_info_t info);
    int32_t (*send_pumped)         (cloud_pumped_info_t event);
    int32_t (*send_printed)        (cloud_pumped_info_t event);
    int32_t (*send_status_updated) (cloud_startup_info_t info);

    // Device identity — returns the UUID provisioned during register_device()
    int32_t (*get_device_uuid)(char *out, uint32_t max_len);

    // Future: routing flags, retry policy, etc. can be added here
    // without touching ModuleCloud.
} cloud_driver_t;
