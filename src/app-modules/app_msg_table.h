// app_msg_table.h
//
// Application message descriptor table.
//
// Descriptors are owned by each typed message class (MsgSensorData,
// MsgTick1000ms, …).  This header assembles them into the flat array
// that hsys_msg_table_init() consumes at startup.
//
// Message IDs are defined in product/app/app_msg_ids.h — never here or in the
// message class files.  This is the only place you manage the full
// descriptor table.
//
// To add a new message:
//   1. Add its ID to product/app/app_msg_ids.h
//   2. Create src/app-messages/msg_<name>.h/.cpp
//   3. #include msg_<name>.h below
//   4. Add MsgXxx::DESCRIPTOR to APP_MSG_TABLE_INIT
//
// Usage in app.cpp:
//   APP_MSG_TABLE_INIT;
//   hsys_msg_table_init(k_msg_table, k_msg_table_count);

#ifndef APP_MSG_TABLE_H
#define APP_MSG_TABLE_H

#include "hsys_msg.h"           // hsys_msg_desc_t
#include "app_msg_ids.h"        // MSG_ID_* — single source of truth for IDs
#include "msg_sensor_data.h"    // MsgSensorData
#include "msg_tick_1000ms.h"    // MsgTick1000ms

// ---------------------------------------------------------------------------
// Descriptor table macro
// Place APP_MSG_TABLE_INIT; in exactly one .cpp file (app.cpp).
// ---------------------------------------------------------------------------

#define APP_MSG_TABLE_INIT                                                        \
    static const hsys_msg_desc_t k_msg_table[] = {                               \
        MsgSensorData::DESCRIPTOR,                                                \
        MsgTick1000ms::DESCRIPTOR,                                                \
    };                                                                            \
    static const uint16_t k_msg_table_count =                                    \
        (uint16_t)(sizeof(k_msg_table) / sizeof(k_msg_table[0]))

#endif // APP_MSG_TABLE_H

