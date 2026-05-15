// app_msg_ids.h
//
// *** Single source of truth for all application message IDs. ***
//
// Rules:
//   1. Every message ID used anywhere in this application MUST be listed here.
//   2. IDs are never duplicated — the enum enforces uniqueness at compile time.
//   3. IDs are grouped by subsystem in ascending order within each domain range.
//   4. Message class headers (msg_*.h) #include this file and reference the
//      named constant — they never contain raw numeric literals for IDs.
//   5. app_msg_table.h assembles the descriptor table using these IDs.
//
// ID space layout (domain-prefix scheme, all IDs < 1024 = 0x0400):
//   0x0000           — reserved (HSYS_MSG_ID_INVALID)
//   0x0010 – 0x001F  — fuel / dispenser messages
//   0x0020 – 0x002F  — button messages
//   0x0030 – 0x003F  — connectivity + OTA + MQTT messages
//   0x0040 – 0x004F  — device info messages
//   0x0050 – 0x005F  — HTTP client session messages
//   0x0060 – 0x00FF  — reserved for future use
//   0x0100 – 0x01FF  — timer messages
//   0x0200 – 0x02FF  — system / timing messages
//   0x0300 – 0x03FF  — config messages
//
// Memory cost of the domain-prefix scheme:
//   The subscription lookup uses a two-array design in hsys_msg.cpp:
//     s_id_map[HSYS_MAX_MSG_IDS]           — uint8_t per slot (1024 × 1 B = 1 KB)
//     s_sub_table[HSYS_MAX_REGISTERED_MSGS] — 18 B per registered msg (64 × 18 B = 1.1 KB)
//   Total: ~2.1 KB, regardless of ID sparseness.

#ifndef APP_MSG_IDS_H
#define APP_MSG_IDS_H

#include "hsys_types.h"   // hsys_msg_id_t

// ---------------------------------------------------------------------------
// Application message ID registry
// The enum underlying type matches hsys_msg_id_t (uint16_t).
// Duplicate values are a compile error — guaranteed uniqueness.
// ---------------------------------------------------------------------------

typedef enum : uint16_t
{
    // ------------------------------------------------------------------
    // Fuel / dispenser  (0x0010 – 0x001F)
    // ------------------------------------------------------------------
    MSG_ID_FUEL_PUMPED          = 0x0010,   ///< ModuleFuel -> all: complete fueling transaction
    MSG_ID_NOZZLE_STATE         = 0x0011,   ///< ModuleFuel -> all: nozzle state transition
    MSG_ID_FUEL_PRINT_OK        = 0x0012,   ///< Any -> all: receipt print confirmed (carries ABS_ID + timestamp)

    // ------------------------------------------------------------------
    // Buttons  (0x0020 – 0x002F)
    // ------------------------------------------------------------------
    MSG_ID_DEFAULT_BTN          = 0x0020,   ///< ModuleDefaultBtn -> all: default button pressed
    MSG_ID_PRINTER_BTN          = 0x0021,   ///< ModulePrintBtn   -> all: print button pressed

    // ------------------------------------------------------------------
    // Connectivity + OTA + MQTT  (0x0030 – 0x003F)
    // ------------------------------------------------------------------
    MSG_ID_WIFI_EVENT           = 0x0030,   ///< ModuleWifi     -> all: WiFi state change
    MSG_ID_INTERNET_STATUS      = 0x0031,   ///< ModuleInternet -> all: internet reachability
    MSG_ID_CUBESPHERE_STATUS    = 0x0032,   ///< ModuleCubeSphere -> all: cloud event result
    MSG_ID_OTA_START_REQUEST    = 0x0033,   ///< Source -> OtaModule:  request OTA session
    MSG_ID_OTA_START_RESPONSE   = 0x0034,   ///< OtaModule -> Source:  session grant/reject
    MSG_ID_OTA_REQUEST_DRIVER   = 0x0035,   ///< Source -> OtaModule:  get fs driver
    MSG_ID_OTA_DRIVER_RESPONSE  = 0x0036,   ///< OtaModule -> Source:  driver + ctx pointers
    MSG_ID_OTA_ABORT_REQUEST    = 0x0037,   ///< Source -> OtaModule:  abort active session
    MSG_ID_OTA_COMPLETE_NOTIFY  = 0x0038,   ///< Source -> OtaModule:  binary write finished
    MSG_ID_OTA_EVENT            = 0x0039,   ///< OtaModule -> all:     session lifecycle events
    MSG_ID_OTA_PROGRESS         = 0x003A,   ///< Source -> all:        write progress update
    MSG_ID_MQTT_STATUS          = 0x003B,   ///< ModuleMqtt -> all: MQTT broker connection state change

    // ------------------------------------------------------------------
    // Device info  (0x0040 – 0x004F)
    // ------------------------------------------------------------------
    MSG_ID_DEV_INFO_READ        = 0x0040,   ///< Any -> ModuleDeviceInfo: read a field by key (DIRECT response)
    MSG_ID_DEV_INFO_WRITE       = 0x0041,   ///< Permitted -> ModuleDeviceInfo: write a field by key
    MSG_ID_DEV_INFO_VALUE       = 0x0042,   ///< ModuleDeviceInfo -> requester: field value response (DIRECT)
    MSG_ID_DEV_INFO_MODIFIED    = 0x0043,   ///< ModuleDeviceInfo -> all: a field was updated (key only)

    // ------------------------------------------------------------------
    // HTTP client session  (0x0050 – 0x005F)
    // ------------------------------------------------------------------
    MSG_ID_HTTP_START_REQUEST      = 0x0050,   ///< client → ModuleHttp: open session (DIRECT)
    MSG_ID_HTTP_START_RESPONSE     = 0x0051,   ///< ModuleHttp → client: session result (DIRECT)
    MSG_ID_HTTP_SET_URL_REQUEST    = 0x0052,   ///< client → ModuleHttp: set request URL (DIRECT)
    MSG_ID_HTTP_SET_URL_RESPONSE   = 0x0053,   ///< ModuleHttp → client: URL accepted (DIRECT)
    MSG_ID_HTTP_SET_ROOT_CA_REQ    = 0x0054,   ///< client → ModuleHttp: set CA cert (DIRECT)
    MSG_ID_HTTP_SET_ROOT_CA_RESP   = 0x0055,   ///< ModuleHttp → client: CA accepted (DIRECT)
    MSG_ID_HTTP_HEADER_REQUEST     = 0x0056,   ///< client → ModuleHttp: add request header (DIRECT)
    MSG_ID_HTTP_HEADER_RESPONSE    = 0x0057,   ///< ModuleHttp → client: header accepted (DIRECT)
    MSG_ID_HTTP_BODY_REQUEST       = 0x0058,   ///< client → ModuleHttp: set request body (DIRECT)
    MSG_ID_HTTP_BODY_RESPONSE      = 0x0059,   ///< ModuleHttp → client: body accepted (DIRECT)
    MSG_ID_HTTP_SEND_REQUEST       = 0x005A,   ///< client → ModuleHttp: execute HTTP call (DIRECT)
    MSG_ID_HTTP_RESULT             = 0x005B,   ///< ModuleHttp → client: HTTP result + body (DIRECT)
    MSG_ID_HTTP_ABORT_REQUEST      = 0x005C,   ///< client → ModuleHttp: abort session (DIRECT)
    MSG_ID_HTTP_RESPONSE_HEADER    = 0x005D,   ///< ModuleHttp → client: one response header (DIRECT)
    MSG_ID_HTTP_SET_STREAM_SINK    = 0x005E,   ///< client → ModuleHttp: set streaming binary write sink (DIRECT)

    // ------------------------------------------------------------------
    // Timer  (0x0100 – 0x010F)
    // ------------------------------------------------------------------
    MSG_ID_TIMER_START          = 0x0100,   ///< Any -> ModuleTimer: start a timer slot
    MSG_ID_TIMER_STOP           = 0x0101,   ///< Any -> ModuleTimer: stop a timer slot
    MSG_ID_TIMER_START_RESPONSE = 0x0102,   ///< ModuleTimer -> requester: start result (DIRECT)
    MSG_ID_TIMER_STOP_RESPONSE  = 0x0103,   ///< ModuleTimer -> requester: stop result (DIRECT)
    MSG_ID_TIMER_ALARM          = 0x0104,   ///< ModuleTimer -> registered module: alarm fired (DIRECT)

    // ------------------------------------------------------------------
    // System / timing  (0x0200 – 0x02FF)
    // ------------------------------------------------------------------
    MSG_ID_TICK_1000MS      = 0x0200,   ///< Ticker -> all: 1 s heartbeat
    MSG_ID_SPIFFS_READY     = 0x0201,   ///< ModuleSpiffs  -> all: SPIFFS mounted and ready
    MSG_ID_SD_READY         = 0x0202,   ///< ModuleSD      -> all: SD card mounted and ready
    MSG_ID_SD_STATUS        = 0x0203,   ///< ModuleSD      -> all: SD card info (type, size, free)
    MSG_ID_TIME_STATUS      = 0x0204,   ///< ModuleTimeMgr -> all: current time source and validity
    MSG_ID_PERSIST_LOG      = 0x0205,   ///< Any -> ModulePLog: write a text string to the SD-card log
    MSG_ID_SYSTEM_STATUS    = 0x0206,   ///< ModuleMsgTranslator -> all: aggregated system busy/idle state

    // ------------------------------------------------------------------
    // Config  (0x0300 – 0x03FF)
    // ------------------------------------------------------------------
    MSG_ID_CONFIG_READY         = 0x0300,   ///< ModuleConfig -> all: config is loaded / updated
    MSG_ID_CONFIG_SET           = 0x0301,   ///< Any -> ModuleConfig: set one config field
    MSG_ID_CONFIG_GET           = 0x0302,   ///< Any -> ModuleConfig: request re-publish of current config

    // Typed domain config requests
    MSG_ID_CONFIG_GET_WIFI      = 0x0303,   ///< Any -> ModuleConfig: request WiFi config (DIRECT response)
    MSG_ID_CONFIG_GET_CLOUD     = 0x0304,   ///< Any -> ModuleConfig: request Cloud config (DIRECT response)
    MSG_ID_CONFIG_GET_MQTT      = 0x0305,   ///< Any -> ModuleConfig: request MQTT config (DIRECT response)
    MSG_ID_CONFIG_GET_DT        = 0x0306,   ///< Any -> ModuleConfig: request Device/HW config (DIRECT response)
    MSG_ID_CONFIG_GET_OTA       = 0x030B,   ///< Any -> ModuleConfig: request OTA config (DIRECT response)
    MSG_ID_CONFIG_GET_KEY       = 0x030D,   ///< Any -> ModuleConfig: request one field by CFG_KEY_* (DIRECT response)

    // Typed domain config responses
    MSG_ID_CONFIG_WIFI          = 0x0307,   ///< ModuleConfig -> ModuleWifi:  WiFi credentials
    MSG_ID_CONFIG_CLOUD         = 0x0308,   ///< ModuleConfig -> ModuleCloud: cloud parameters
    MSG_ID_CONFIG_MQTT          = 0x0309,   ///< ModuleConfig -> ModuleMqtt:  MQTT broker settings
    MSG_ID_CONFIG_DT            = 0x030A,   ///< ModuleConfig -> ModuleFuel:  display-type / HW settings
    MSG_ID_CONFIG_OTA           = 0x030C,   ///< ModuleConfig -> ModuleWebClientOta: OTA server config
    MSG_ID_CONFIG_VALUE         = 0x030E,   ///< ModuleConfig -> requester: single-field binary value response
    MSG_ID_CONFIG_UPDATED       = 0x030F,   ///< ModuleConfig -> all: a config field was written (carries CFG_KEY_*)

    // ------------------------------------------------------------------
    // Sentinel — keep one above the highest assigned ID
    // ------------------------------------------------------------------
    MSG_ID_MAX              = 0x0310,   ///< highest used is 0x030F (CONFIG_UPDATED)

} app_msg_id_e;

#endif // APP_MSG_IDS_H
