// app_msg_ids.h
//
// *** Single source of truth for all application message IDs. ***
//
// Rules:
//   1. Every message ID used anywhere in this application MUST be listed here.
//   2. IDs are never duplicated — the enum enforces uniqueness at compile time.
//   3. IDs are grouped by subsystem in ascending order within each range.
//   4. Message class headers (msg_*.h) #include this file and reference the
//      named constant — they never contain raw numeric literals for IDs.
//   5. app_msg_table.h assembles the descriptor table using these IDs.
//
// ID space layout:
//   0x0000           — reserved (HSYS_MSG_ID_INVALID)
//   0x0001 – 0x00FF  — sensor / data messages
//   0x0100 – 0x01FF  — control / command messages
//   0x0200 – 0x02FF  — system / timing messages
//   0x0300 – 0x03FF  — config messages
//   0x0400 – 0xFFFE  — reserved for future use
//   0xFFFF           — reserved (HSYS_MSG_ID_INVALID)

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
    // Sensor / data  (0x0001 – 0x00FF)
    // ------------------------------------------------------------------
    MSG_ID_SENSOR_DATA      = 0x0001,   ///< ModuleA -> ModuleB: sensor reading

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

    // ------------------------------------------------------------------
    // Config  (0x0300 – 0x03FF)
    // ------------------------------------------------------------------
    MSG_ID_CONFIG_READY         = 0x0300,   ///< ModuleConfig -> all: config is loaded / updated
    MSG_ID_CONFIG_SET           = 0x0301,   ///< Any -> ModuleConfig: set one config field
    MSG_ID_CONFIG_GET           = 0x0302,   ///< Any -> ModuleConfig: request re-publish of current config

    // Typed domain config requests (sent AFTER MsgConfigReady — NOTIFICATION)
    MSG_ID_CONFIG_GET_WIFI      = 0x0303,   ///< Any -> ModuleConfig: request WiFi config (DIRECT response)
    MSG_ID_CONFIG_GET_CLOUD     = 0x0304,   ///< Any -> ModuleConfig: request Cloud config (DIRECT response)
    MSG_ID_CONFIG_GET_MQTT      = 0x0305,   ///< Any -> ModuleConfig: request MQTT config (DIRECT response)
    MSG_ID_CONFIG_GET_DT        = 0x0306,   ///< Any -> ModuleConfig: request Device/HW config (DIRECT response)
    MSG_ID_CONFIG_GET_OTA       = 0x030B,   ///< Any -> ModuleConfig: request OTA config (DIRECT response)

    // Typed domain config responses (sent DIRECT back to the requester)
    MSG_ID_CONFIG_WIFI          = 0x0307,   ///< ModuleConfig -> ModuleWifi:  WiFi credentials
    MSG_ID_CONFIG_CLOUD         = 0x0308,   ///< ModuleConfig -> ModuleCloud: cloud parameters
    MSG_ID_CONFIG_MQTT          = 0x0309,   ///< ModuleConfig -> ModuleMqtt:  MQTT broker settings
    MSG_ID_CONFIG_DT            = 0x030A,   ///< ModuleConfig -> ModuleFuel:  display-type / HW settings
    MSG_ID_CONFIG_OTA           = 0x030C,   ///< ModuleConfig -> ModuleWebClientOta: OTA server config

    // ------------------------------------------------------------------
    // Fuel / dispenser  (0x0800 – 0x08FF)
    // ------------------------------------------------------------------
    MSG_ID_FUEL_PUMPED          = 0x0800,   ///< ModuleFuel -> all: complete fueling transaction
    MSG_ID_NOZZLE_STATE         = 0x0801,   ///< ModuleFuel -> all: nozzle state transition

    // ------------------------------------------------------------------
    // Buttons  (0x0900 – 0x09FF)
    // ------------------------------------------------------------------
    MSG_ID_DEFAULT_BTN          = 0x0900,   ///< ModuleDefaultBtn -> all: default button pressed
    MSG_ID_PRINTER_BTN          = 0x0901,   ///< ModulePrintBtn   -> all: print button pressed

    // ------------------------------------------------------------------
    // Connectivity  (0x0A00 – 0x0AFF)
    // ------------------------------------------------------------------
    MSG_ID_WIFI_EVENT           = 0x0A00,   ///< ModuleWifi     -> all: WiFi state change
    MSG_ID_INTERNET_STATUS      = 0x0A01,   ///< ModuleInternet -> all: internet reachability
    MSG_ID_CLOUD_STATUS         = 0x0A02,   ///< ModuleCloud    -> all: cloud event result

    // ------------------------------------------------------------------
    // OTA  (0x0A03 – 0x0A0A)
    // ------------------------------------------------------------------
    MSG_ID_OTA_START_REQUEST    = 0x0A03,   ///< Source -> OtaModule:  request OTA session
    MSG_ID_OTA_START_RESPONSE   = 0x0A04,   ///< OtaModule -> Source:  session grant/reject
    MSG_ID_OTA_REQUEST_DRIVER   = 0x0A05,   ///< Source -> OtaModule:  get fs driver
    MSG_ID_OTA_DRIVER_RESPONSE  = 0x0A06,   ///< OtaModule -> Source:  driver + ctx pointers
    MSG_ID_OTA_ABORT_REQUEST    = 0x0A07,   ///< Source -> OtaModule:  abort active session
    MSG_ID_OTA_COMPLETE_NOTIFY  = 0x0A08,   ///< Source -> OtaModule:  binary write finished
    MSG_ID_OTA_EVENT            = 0x0A09,   ///< OtaModule -> all:     session lifecycle events
    MSG_ID_OTA_PROGRESS         = 0x0A0A,   ///< Source -> all:        write progress update

    // ------------------------------------------------------------------
    // Sentinel — keep one above the highest assigned ID
    // ------------------------------------------------------------------
    MSG_ID_MAX              = 0x0A0B,

} app_msg_id_e;

#endif // APP_MSG_IDS_H
