// app_module_ids.h  (src/app-modules mirror)
//
// This file is a verbatim mirror of src/product/app/app_module_ids.h.
// The two copies exist so that:
//   • Module headers in src/app-modules/ can #include "app_module_ids.h"
//     without a long relative path.
//   • The product layer (src/product/app/) has its own copy for the same reason.
//
// Keep both files in sync — they must be identical.

#ifndef APP_MODULE_IDS_H
#define APP_MODULE_IDS_H

#include "hsys_types.h"   // hsys_module_id_t

// ------------------------------------------------------------------
// Demo / test  (1 – 2)  — not used in production builds
// ------------------------------------------------------------------
#define MODULE_A_ID               ((hsys_module_id_t)  1)   ///< ModuleA  — demo
#define MODULE_B_ID               ((hsys_module_id_t)  2)   ///< ModuleB  — demo

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
// Simulator-only / platform-specific  (20+)
// ------------------------------------------------------------------
#define MODULE_SIM_BRIDGE_ID      ((hsys_module_id_t) 20)   ///< ModuleSimBridge — TCP UI bridge (sim only)

#endif // APP_MODULE_IDS_H
