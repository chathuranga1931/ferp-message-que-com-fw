// app_module_ids.h
//
// *** Single source of truth for all application module IDs. ***
//
// Rules:
//   1. Every module ID used anywhere in this application MUST be listed here.
//   2. IDs are never duplicated — each value appears exactly once.
//   3. IDs are assigned in ascending order; gaps are intentional and reserved.
//   4. Module class headers (#include this file) reference the named constant
//      — they never contain raw numeric literals for IDs.
//   5. 0 is reserved (HSYS_MODULE_ID_INVALID). Simulator-only modules use
//      IDs ≥ 20 to keep them clearly separate from shared app modules.
//
// ID assignments:
//   0        — reserved (HSYS_MODULE_ID_INVALID)
//   1  – 2   — reserved (formerly demo/test modules; removed)
//   3  – 16  — shared application modules
//   17 – 19  — reserved for future shared modules
//   20+      — simulator-only / platform-specific modules

#ifndef APP_MODULE_IDS_H
#define APP_MODULE_IDS_H

#include "hsys_types.h"   // hsys_module_id_t

// ------------------------------------------------------------------
// Shared application modules  (3 – 16)
// ------------------------------------------------------------------
#define TICKER_MODULE_ID          ((hsys_module_id_t)  3)   ///< Ticker          — 1 s heartbeat generator
#define MODULE_SYSMON_ID          ((hsys_module_id_t)  4)   ///< ModuleSysmon    — pool / stats reporter
#define MODULE_SPIFFS_ID          ((hsys_module_id_t)  5)   ///< ModuleSpiffs    — SPIFFS filesystem
#define MODULE_CONFIG_ID          ((hsys_module_id_t)  6)   ///< ModuleConfig    — persistent config
#define MODULE_TIMER_ID           ((hsys_module_id_t)  7)   ///< ModuleTimer     — software timer slots
#define MODULE_LEDS_ID            ((hsys_module_id_t)  8)   ///< ModuleLeds      — status LEDs
#define MODULE_DEFAULT_BTN_ID     ((hsys_module_id_t)  9)   ///< ModuleDefaultBtn — default button
#define MODULE_PRINT_BTN_ID       ((hsys_module_id_t) 10)   ///< ModulePrintBtn  — print button
#define MODULE_FUEL_ID            ((hsys_module_id_t) 11)   ///< ModuleFuel      — fuel dispenser
#define MODULE_BUZZER_ID          ((hsys_module_id_t) 12)   ///< ModuleBuzzer    — buzzer / audio cues
#define MODULE_CLOUD_ID           ((hsys_module_id_t) 13)   ///< ModuleCloud     — cloud connectivity
#define MODULE_INTERNET_ID        ((hsys_module_id_t) 14)   ///< ModuleInternet  — internet reachability
#define MODULE_WIFI_ID            ((hsys_module_id_t) 15)   ///< ModuleWifi      — WiFi connection manager
#define MODULE_SD_ID              ((hsys_module_id_t) 16)   ///< ModuleSD        — SD card

// ------------------------------------------------------------------
// Shared modules  (17 – 19)
// ------------------------------------------------------------------
#define MODULE_TIMEMGR_ID         ((hsys_module_id_t) 17)   ///< ModuleTimeMgr       — real-time clock manager
#define MODULE_OTA_ID             ((hsys_module_id_t) 18)   ///< ModuleOta           — OTA session manager
#define MODULE_WEB_CLIENT_OTA_ID  ((hsys_module_id_t) 19)   ///< ModuleWebClientOta  — cloud-polling OTA source

// ------------------------------------------------------------------
// Shared application modules  (23+)
// ------------------------------------------------------------------
#define MODULE_MSG_TRANSLATOR_ID  ((hsys_module_id_t) 23)   ///< ModuleMsgTranslator — message translation and routing
#define MODULE_DEVICE_INFO_ID     ((hsys_module_id_t) 24)   ///< ModuleDeviceInfo    — runtime device identity
#define MODULE_PLOG_ID            ((hsys_module_id_t) 25)   ///< ModulePLog          — persistent SD-card logger

// ------------------------------------------------------------------
// Simulator-only / platform-specific  (20+)
// ------------------------------------------------------------------
#define MODULE_SIM_BRIDGE_ID      ((hsys_module_id_t) 20)   ///< ModuleSimBridge — TCP UI bridge (sim only)
#define MODULE_WEB_SERVER_ID      ((hsys_module_id_t) 21)   ///< ModuleWebServer — HTTP config server (sim only, port 8080)
#define MODULE_MQTT_ID            ((hsys_module_id_t) 22)   ///< ModuleMqtt      — MQTT broker client

#endif // APP_MODULE_IDS_H
