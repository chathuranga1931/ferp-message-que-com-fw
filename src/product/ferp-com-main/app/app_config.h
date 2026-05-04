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

    // OTA server (separate endpoint from the cloud API)
    char     ota_server_url[128];
    uint32_t ota_check_interval_s;  ///< How often to poll for updates (seconds, 30–300)
    bool     cloud_hb_enabled;
    uint32_t cloud_hb_interval_s;

    // MQTT
    char     mqtt_host[64];
    uint32_t mqtt_port;
    char     mqtt_user[32];
    char     mqtt_password[64];

    // Hardware
    uint32_t display_type;
    uint32_t stabilize_delay_ms;
    bool     en_retx;               ///< Enable display tap retransmission
    bool     nozzle_swap;           ///< Swap nozzle 1 and 2 assignment
    uint32_t tot_cnt;               ///< Totalizer stabilisation count
    uint32_t tot_dur;               ///< Totalizer stabilisation duration (ms)

    // Printer
    char     printer_url[128];
    uint32_t printer_copy_count;
    uint32_t print_delay_ms;        ///< Delay between print trigger and print send (ms)

    // Logging
    bool     log_udp_enabled;
    char     log_udp_server_ip[16];
    uint32_t log_udp_port;
    uint32_t dt_log_rate;           ///< Data transaction log rate (every N transactions)

    // Application feature flags
    bool     enable_nid_print;      ///< Enable NID on printed receipts
    bool     enable_nid_cloud;      ///< Enable NID in cloud uploads

} app_config_t;

// ---------------------------------------------------------------------------
// Config keys — 16-bit identifiers organised by section.
//
// Ranges:
//   0x1000–0x10FF  WiFi
//   0x2000–0x20FF  Cloud / HTTP
//   0x3000–0x30FF  OTA
//   0x4000–0x40FF  MQTT
//   0x5000–0x50FF  Device identity
//   0x6000–0x60FF  Hardware
//   0x7000–0x70FF  Printer
//   0x8000–0x80FF  Logging
//   0x9000–0x90FF  Application feature flags
//
// Modules will use these keys to fetch individual config values in Phase 2.
// ---------------------------------------------------------------------------

// WiFi
#define CFG_KEY_WIFI_SSID              0x1001u
#define CFG_KEY_WIFI_PASSWORD          0x1002u

// Cloud / HTTP
#define CFG_KEY_CLOUD_URL              0x2001u
#define CFG_KEY_CLOUD_SECRET           0x2002u
#define CFG_KEY_CLOUD_HB_ENABLED       0x2003u
#define CFG_KEY_CLOUD_HB_INTERVAL_S    0x2004u

// OTA
#define CFG_KEY_OTA_SERVER_URL         0x3001u
#define CFG_KEY_OTA_CHECK_INTERVAL_S   0x3002u

// MQTT
#define CFG_KEY_MQTT_HOST              0x4001u
#define CFG_KEY_MQTT_PORT              0x4002u
#define CFG_KEY_MQTT_USER              0x4003u
#define CFG_KEY_MQTT_PASSWORD          0x4004u

// Hardware
#define CFG_KEY_DISPLAY_TYPE           0x6001u
#define CFG_KEY_STABILIZE_DELAY_MS     0x6002u
#define CFG_KEY_EN_RETX                0x6003u
#define CFG_KEY_NOZZLE_SWAP            0x6004u
#define CFG_KEY_TOT_CNT                0x6005u
#define CFG_KEY_TOT_DUR                0x6006u

// Printer
#define CFG_KEY_PRINTER_URL            0x7001u
#define CFG_KEY_PRINTER_COPY_COUNT     0x7002u
#define CFG_KEY_PRINT_DELAY_MS         0x7003u

// Logging
#define CFG_KEY_LOG_UDP_ENABLED        0x8001u
#define CFG_KEY_LOG_UDP_SERVER_IP      0x8002u
#define CFG_KEY_LOG_UDP_PORT           0x8003u
#define CFG_KEY_DT_LOG_RATE            0x8004u

// Application feature flags
#define CFG_KEY_ENABLE_NID_PRINT       0x9001u
#define CFG_KEY_ENABLE_NID_CLOUD       0x9002u

#ifdef __cplusplus
extern "C" {
#endif

/** Fill cfg with compiled-in defaults.  Call before loading from file. */
void app_config_load_defaults(app_config_t *cfg);

#ifdef __cplusplus
}
#endif
