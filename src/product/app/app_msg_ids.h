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
    // System / timing  (0x0200 – 0x02FF)
    // ------------------------------------------------------------------
    MSG_ID_TICK_1000MS      = 0x0200,   ///< Ticker -> all: 1 s heartbeat
    MSG_ID_SPIFFS_READY     = 0x0201,   ///< ModuleSpiffs -> all: SPIFFS mounted and ready

    // ------------------------------------------------------------------
    // Config  (0x0300 – 0x03FF)
    // ------------------------------------------------------------------
    MSG_ID_CONFIG_READY         = 0x0300,   ///< ModuleConfig -> all: config is loaded / updated
    MSG_ID_CONFIG_SET           = 0x0301,   ///< Any -> ModuleConfig: set one config field
    MSG_ID_CONFIG_GET_REQUEST   = 0x0302,   ///< Any -> ModuleConfig: request re-publish of current config

    // ------------------------------------------------------------------
    // Sentinel — keep one above the highest assigned ID
    // ------------------------------------------------------------------
    MSG_ID_MAX              = 0x0303,

} app_msg_id_e;

#endif // APP_MSG_IDS_H
