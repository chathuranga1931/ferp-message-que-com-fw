// timer_types.h
//
// Shared types used by the timer message classes and ModuleTimer.
// Lives in app-messages/ so both message headers and the module can
// include it without a circular dependency.

#ifndef TIMER_TYPES_H
#define TIMER_TYPES_H

#include "hsys_types.h"   // hsys_module_id_t, hsys_tick_t

// ---------------------------------------------------------------------------
// timer_result_t — outcome codes carried in response messages
// ---------------------------------------------------------------------------

typedef enum : uint8_t {
    TIMER_RESULT_OK                  = 0,   ///< Operation succeeded
    TIMER_RESULT_ERR_ALREADY_RUNNING = 1,   ///< Start: slot exists for this module
    TIMER_RESULT_ERR_NOT_FOUND       = 2,   ///< Stop : no active slot for this module
    TIMER_RESULT_ERR_SLOTS_FULL      = 3,   ///< Start: all k_max_timers slots occupied
    TIMER_RESULT_ERR_INVALID_PARAM   = 4,   ///< Zero duration or invalid module_id
} timer_result_t;

// ---------------------------------------------------------------------------
// timer_meta_t — one active timer slot inside ModuleTimer
// ---------------------------------------------------------------------------

typedef struct {
    hsys_module_id_t  source_id;          ///< Owning module
    uint32_t          duration_ms;        ///< Period (repetitive) or one-shot duration
    uint32_t          user_tag;           ///< Opaque tag supplied by caller; echoed in MsgTimerAlarm
    bool              is_repetitive;      ///< Repeats if true
    bool              active;             ///< Slot is in use
    uint64_t          next_fire_ms;       ///< Absolute uptime timestamp for next alarm
} timer_meta_t;

#endif // TIMER_TYPES_H
