// sanki_6_digit_1.h
//
// Ported from old-app/application/app_fuel/pumps/sanki/sanki_6_digit_1.h
// Include path changed: app_display_data_t now comes from fuel_types.h.

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fuel_types.h"      // app_display_data_t, NO_NOZZELS
#include "nozzle_event.h"    // nozzle_event_t

#include "pumping_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool sanki6_get_event(nozzle_event_t *ne, uint8_t idx);
bool sanki6_process_state_machine(const app_display_data_t *display_data, uint8_t nozzle_id, pumping_state_t *state);
bool sanki6_process_data(app_display_data_t *display_data);
void sanki6_data_validate(const app_display_data_t *display_data, uint8_t nozzle_id);

#ifdef __cplusplus
}
#endif
