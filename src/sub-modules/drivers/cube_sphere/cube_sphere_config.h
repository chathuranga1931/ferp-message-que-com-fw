// cube_sphere_config.h
//
// Self-contained constants and error codes required by cube_sphere_api.
//
// This header replaces the old dependency on app.h / device_config.h so the
// driver folder has no dependency on the application layer.

#pragma once

#include <stdint.h>

// ── String buffer sizes ───────────────────────────────────────────────────────

#define SIZE_OF_NTWK_BASE_URL     255
#define SIZE_OF_WIFI_SSID          50
#define SIZE_OF_WIFI_PASSWORD      50
#define SIZE_OF_SECRET            255
#define SIZE_OF_UUID               50
#define SIZE_OF_NOZZELID           10
#define SIZE_OF_FUEL_TYPE          10
#define SIZE_OF_MAC                25
#define SIZE_OF_IPADDRESS          25
#define SIZE_OF_IP_ADDRESS         25
#define SIZE_OF_FUEL_TYPE_STR      35
#define SIZE_OF_STATUS_WORD_STR    20
#define SIZE_OF_INT_STR            20
#define SIZE_OF_VERSION_STR        30
#define SIZE_OF_DEVICE_TYPE        30

// ── Hardware config ───────────────────────────────────────────────────────────

#define NO_NOZZELS   2

// ── Error codes ───────────────────────────────────────────────────────────────

#ifndef ERROR_OK
#define ERROR_OK  0
#endif

#define ERROR_APP_CLOUD_INVALID_MAC_ADDRESS       4
#define ERROR_APP_CLOUD_NO_NONCE                  5
#define ERROR_APP_CLOUD_GET_AGENT_CONFIG_FAILED   6
#define ERROR_APP_CLOUD_GET_NOZZLE_CONFIG_FAILED  7
