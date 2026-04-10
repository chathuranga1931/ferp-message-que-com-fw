// app_msg_table.h
//
// Application message descriptor table.
//
// Every message in the system is declared here as an hsys_msg_desc_t entry.
// The descriptor captures everything the framework needs at compile time:
//   msg_id        — unique numeric ID (used as table index into the bus)
//   msg_type      — NOTIFICATION (fan-out to all subscribers)
//                   DIRECT       (point-to-point, receiver_id must be set)
//   payload_size  — bytes the framework allocates from the pool on create();
//                   0 = no payload (e.g. heartbeat ticks)
//   pub_perm      — which module IDs may publish  (HSYS_PERM_ANY = unrestricted)
//   sub_perm      — which module IDs may subscribe (HSYS_PERM_ANY = unrestricted)
//
// Usage in app.cpp:
//   APP_MSG_TABLE_INIT;                          // declares k_msg_table[]
//   hsys_msg_table_init(k_msg_table, k_msg_table_count);
//
// Modules only need to #include this header to get the MSG_* constants.
// They never call hsys_pool_alloc / hsys_pool_free directly.

#ifndef APP_MSG_TABLE_H
#define APP_MSG_TABLE_H

#include "hsys_msg.h"   // hsys_msg_desc_t, HSYS_MSG_DESC, hsys_msg_type_t

// ---------------------------------------------------------------------------
// Message IDs
// ---------------------------------------------------------------------------

/** Published by Module A.  Payload: module_a_sensor_data_t (8 bytes) */
#define MSG_SENSOR_DATA     ((hsys_msg_id_t)0x0001)

/** Published by Ticker every 1 000 ms.  No payload (payload_size = 0). */
#define MSG_TICK_1000MS     ((hsys_msg_id_t)0x0200)

/** Sentinel — one greater than the highest defined msg_id. */
#define APP_MSG_ID_MAX      ((hsys_msg_id_t)0x0201)

// ---------------------------------------------------------------------------
// Message descriptor table
// ---------------------------------------------------------------------------
//
// Place APP_MSG_TABLE_INIT; in exactly one .cpp file (app.cpp) to emit the
// static descriptor array that is passed to hsys_msg_table_init().

#define APP_MSG_TABLE_INIT                                                    \
    static const hsys_msg_desc_t k_msg_table[] = {                           \
        /*             id               type                    payload_size  pub_perm        sub_perm    */ \
        HSYS_MSG_DESC(MSG_SENSOR_DATA,  HSYS_MSG_NOTIFICATION,  8,            HSYS_PERM_ANY,  HSYS_PERM_ANY), \
        HSYS_MSG_DESC(MSG_TICK_1000MS,  HSYS_MSG_NOTIFICATION,  0,            HSYS_PERM_ANY,  HSYS_PERM_ANY), \
    };                                                                        \
    static const uint16_t k_msg_table_count =                                \
        (uint16_t)(sizeof(k_msg_table) / sizeof(k_msg_table[0]))

#endif // APP_MSG_TABLE_H

