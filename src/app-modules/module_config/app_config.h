// app_config.h
//
// Application configuration structure.
//
// app_config_t is the single in-memory representation of all device
// configuration.  It is owned by ModuleConfig as a static instance.
//
// Field table — app_config_fields.h — is the ONLY file that needs to be
// edited when adding a new config field.  The struct below is generated
// from that table via X-macros.
//
// Do NOT add fields directly here.  Add them to app_config_fields.h.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "hsys_type.h"

// ---------------------------------------------------------------------------
// Config field type — thin alias over hsys_type_t so the message layer
// doesn't need to depend on the peripheral sub-module directly.
// ---------------------------------------------------------------------------

typedef hsys_type_t app_cfg_type_t;

#define APP_CFG_TYPE_UINT32  HSYS_TYPE_UINT32
#define APP_CFG_TYPE_STRING  HSYS_TYPE_STRING
#define APP_CFG_TYPE_BOOL    HSYS_TYPE_BOOL

// ---------------------------------------------------------------------------
// app_config_t  — generated from app_config_fields.h
// ---------------------------------------------------------------------------

typedef struct {

    // WiFi
    char     wifi_ssid[64];
    char     wifi_password[64];

    // Cloud / HTTP
    char     cloud_url[128];
    char     cloud_secret[64];
    bool     cloud_hb_enabled;
    uint32_t cloud_hb_interval_s;

    // MQTT
    char     mqtt_host[64];
    uint32_t mqtt_port;
    char     mqtt_user[32];
    char     mqtt_password[64];

    // Device identity
    char     device_uuid[40];
    char     device_group[32];

    // Hardware
    uint32_t display_type;
    uint32_t stabilize_delay_ms;

    // Printer
    char     printer_url[128];
    uint32_t printer_copy_count;

    // Logging
    bool     log_udp_enabled;
    char     log_udp_server_ip[16];
    uint32_t log_udp_port;

} app_config_t;

#ifdef __cplusplus
extern "C" {
#endif

/** Fill cfg with compiled-in defaults.  Call before loading from file. */
void app_config_load_defaults(app_config_t *cfg);

#ifdef __cplusplus
}
#endif
