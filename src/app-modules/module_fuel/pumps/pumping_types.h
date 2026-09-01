
#pragma once

#include <stdint.h>

// Display-settle wait applied by every pump's Stopped-Waiting state after
// nozzle DOWN: the state machine holds for (PUMP_STABILIZE_BASE_MS +
// g_pump_stabilize_delay_ms) before finalizing the pumped event, so a
// late-settling display value (which can land ~1s after motor-stop on some
// pumps) is captured. This is a non-blocking, state-based wait — frames keep
// arriving and each pump's *_update_data() commits the latest reading during
// the window; it is NOT a pal_delay.
//
// PUMP_STABILIZE_BASE_MS is an always-applied minimum. g_pump_stabilize_delay_ms
// is the per-device config value (stabilize_delay_ms, default 500), set by
// ModuleFuel from MsgConfigDT and live-updated on MsgConfigUpdated.
#define PUMP_STABILIZE_BASE_MS  (500)

#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t g_pump_stabilize_delay_ms;
#ifdef __cplusplus
}
#endif

typedef enum {
    Pumping_State_Unknown = 0,
    Pumping_State_Pumping,
    Pumping_State_Stopped,
    Pumping_State_Pumping_Waiting,
    Pumping_State_Stopped_Waiting,
    Pumping_State_Nozzle_Off_Waiting_To_Stable,
}pumping_state_t;

