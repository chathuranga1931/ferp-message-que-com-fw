// hsys_types.h
//
// Shared primitive types used across the entire HSYS messaging architecture.
// Every other header in src/modules/hsys-arch/ includes this file.
// No RTOS or OS headers are included here — purely portable C99 types.

#ifndef HSYS_TYPES_H
#define HSYS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Status / error codes
// ---------------------------------------------------------------------------

typedef enum {
    HSYS_OK                  =  0,   ///< Success
    HSYS_ERR_NULL            = -1,   ///< Null pointer argument
    HSYS_ERR_INVALID         = -2,   ///< Invalid argument value
    HSYS_ERR_NO_MEM          = -3,   ///< Pool / heap exhausted
    HSYS_ERR_QUEUE_FULL      = -4,   ///< Target task queue is full
    HSYS_ERR_TIMEOUT         = -5,   ///< Operation timed out
    HSYS_ERR_NOT_FOUND       = -6,   ///< Module / subscription not found
    HSYS_ERR_ALREADY_EXISTS  = -7,   ///< Duplicate registration
    HSYS_ERR_NOT_INIT        = -8,   ///< Subsystem not initialised
    HSYS_ERR_GENERIC         = -99,  ///< Unclassified error
} hsys_status_t;

// ---------------------------------------------------------------------------
// Identity types
// ---------------------------------------------------------------------------

/** Unique identifier for a registered Module (0 = invalid/unset). */
typedef uint16_t hsys_module_id_t;

/** Unique identifier for a message type / topic. */
typedef uint16_t hsys_msg_id_t;

// ---------------------------------------------------------------------------
// Message priority
// ---------------------------------------------------------------------------

typedef enum {
    HSYS_PRIORITY_LOW    = 0,
    HSYS_PRIORITY_NORMAL = 1,
    HSYS_PRIORITY_HIGH   = 2,
} hsys_priority_t;

// ---------------------------------------------------------------------------
// Tick / time type
// ---------------------------------------------------------------------------

/** System uptime in milliseconds (sourced from hsys_task_get_tick_ms()). */
typedef uint32_t hsys_tick_t;

/** Pass as timeout_ms to block indefinitely (never time out). */
#define HSYS_WAIT_FOREVER  ((uint32_t)0xFFFFFFFFUL)

// ---------------------------------------------------------------------------
// Convenience macros
// ---------------------------------------------------------------------------

#define HSYS_MODULE_ID_INVALID   ((hsys_module_id_t)0)
#define HSYS_MSG_ID_INVALID      ((hsys_msg_id_t)0)

#ifdef __cplusplus
}
#endif

#endif // HSYS_TYPES_H
