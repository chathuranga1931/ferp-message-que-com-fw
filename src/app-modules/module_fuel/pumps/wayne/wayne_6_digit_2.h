// wayne_6_digit_2.h
//
// Wayne 6-digit Type02 dispenser pump driver.
// Identical logic to wayne_6_digit_1.cpp, with one deliberate difference:
// this pump is configured with a fixed unit price of 1.00 for a specific
// reason (not a real per-liter price), so wayne6_process_data()'s shared
// >=200.00 sanity floor (calibrated for realistic fuel prices) would
// reject every frame. Here the check only rejects a genuinely absent/zero
// unit price, not a low one.
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

bool wayne62_get_event(nozzle_event_t *ne, uint8_t idx);
bool wayne62_process_state_machine(const app_display_data_t *display_data, uint8_t nozzle_id, pumping_state_t *state);
bool wayne62_process_data(app_display_data_t *display_data);
void wayne62_data_validate(const app_display_data_t *display_data, uint8_t nozzle_id);

#ifdef __cplusplus
}
#endif
