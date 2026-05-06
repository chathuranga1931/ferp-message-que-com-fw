// hsys_config.h// hsys_config.h — framework tunable redirect

//// Renamed to hsys_fw_config.h to avoid collision with middleware/hsys_config.h

// Compile-time tunables for the HSYS messaging architecture.// This shim keeps any stale includes working.

// All pool sizes, counts, and limits are defined here.#include "hsys_fw_config.h"

//

// To customise: override any of these defines BEFORE including this header,
// or pass them via compiler flags (-DHSYS_MAX_MODULES=32).
// When using ESP-IDF with Kconfig, generated values from sdkconfig.h will
// eventually map here.

#ifndef HSYS_CONFIG_H
#define HSYS_CONFIG_H

// ---------------------------------------------------------------------------
// Module registry
// ---------------------------------------------------------------------------

/** Maximum number of modules that can be registered at runtime. */
#ifndef HSYS_MAX_MODULES
#define HSYS_MAX_MODULES            16
#endif

// ---------------------------------------------------------------------------
// Task / queue
// ---------------------------------------------------------------------------

/** Maximum number of RTOS tasks managed by the task manager. */
#ifndef HSYS_MAX_TASKS
#define HSYS_MAX_TASKS              8
#endif

/** Default depth (number of messages) of each task's inbox queue. */
#ifndef HSYS_DEFAULT_QUEUE_DEPTH
#define HSYS_DEFAULT_QUEUE_DEPTH    16
#endif

// ---------------------------------------------------------------------------
// Message bus / subscription table
// ---------------------------------------------------------------------------

/** Maximum number of distinct message IDs in the subscription table. */
#ifndef HSYS_MAX_MSG_IDS
// Size the subscription table to cover all message IDs.
// Must be strictly greater than the highest hsys_msg_id_t value used.
// The default of 1024 covers IDs 0x000–0x3FF (four 256-slot domains).
// Increase if your application defines IDs beyond 0x3FF.
#define HSYS_MAX_MSG_IDS            1024
#endif

/** Maximum number of subscribers per message ID. */
#ifndef HSYS_MAX_SUBSCRIBERS_PER_MSG
#define HSYS_MAX_SUBSCRIBERS_PER_MSG  8
#endif

/** Maximum number of message types that can be registered via hsys_msg_table_init().
 *  The subscription table has one entry per registered message type (not per possible
 *  ID value), so this controls the compact s_sub_table size — set it to the number of
 *  messages in your descriptor table plus a small margin.  Must be <= 254 (uint8_t
 *  index; 0xFF is the "not registered" sentinel). */
#ifndef HSYS_MAX_REGISTERED_MSGS
#define HSYS_MAX_REGISTERED_MSGS    64
#endif

// ---------------------------------------------------------------------------
// Buffer pool
//
// Maximum number of size classes the pool manager can handle.
// The actual classes and block counts are supplied as a const table in
// main.c and passed to hsys_pool_init() — nothing is hardcoded here.
// ---------------------------------------------------------------------------

/** Maximum number of pool size classes that can be registered at init time. */
#ifndef HSYS_POOL_CLASS_COUNT
#define HSYS_POOL_CLASS_COUNT       8
#endif

#endif // HSYS_CONFIG_H
