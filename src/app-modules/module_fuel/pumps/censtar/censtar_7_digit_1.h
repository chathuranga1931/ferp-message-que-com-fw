// censtar_7_digit_1.h
//
// Censtar 7-digit dispenser pump driver.
// Covers both DIS_CENSTAR_7_DIGIT and DIS_CENSTAR_7_DIGIT_CS variants —
// both share the same state machine (DT board normalises the protocol).
// GPIO-sourced start_stop injected by module_fuel before calling.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fuel_types.h"      // app_display_data_t, NO_NOZZELS
#include "nozzle_event.h"    // nozzle_event_t
#include "pumping_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool censtar7_get_event(nozzle_event_t *ne, uint8_t idx);
bool censtar7_process_state_machine(const app_display_data_t *display_data, uint8_t nozzle_id, pumping_state_t *state);
bool censtar7_process_data(app_display_data_t *display_data);
void censtar7_data_validate(const app_display_data_t *display_data, uint8_t nozzle_id);

#ifdef __cplusplus
}
#endif
