// hongyang_8_digit_1.h
//
// Hongyang 8-digit dispenser pump driver.
// start_stop is sourced from the display frame (flags.bits.start_stop) —
// module_fuel does NOT override it with GPIO state for this pump type.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fuel_types.h"      // app_display_data_t, NO_NOZZELS
#include "nozzle_event.h"    // nozzle_event_t
#include "pumping_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool hongyang8_get_event(nozzle_event_t *ne, uint8_t idx);
bool hongyang8_process_state_machine(const app_display_data_t *display_data, uint8_t nozzle_id, pumping_state_t *state);
bool hongyang8_process_data(app_display_data_t *display_data);
void hongyang8_data_validate(const app_display_data_t *display_data, uint8_t nozzle_id);

#ifdef __cplusplus
}
#endif
