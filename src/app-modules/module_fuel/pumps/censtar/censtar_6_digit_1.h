// censtar_6_digit_1.h
//
// Censtar 6-digit dispenser pump driver.
// Ported to the new architecture — identical state machine to sanki_6_digit_1,
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

bool censtar6_get_event(nozzle_event_t *ne, uint8_t idx);
bool censtar6_process_state_machine(const app_display_data_t *display_data, uint8_t nozzle_id, pumping_state_t *state);
bool censtar6_process_data(app_display_data_t *display_data);
void censtar6_data_validate(const app_display_data_t *display_data, uint8_t nozzle_id);

#ifdef __cplusplus
}
#endif
