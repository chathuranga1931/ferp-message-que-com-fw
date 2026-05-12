// longfeng_8_digit_1.h
//
// Longfeng 8-digit dispenser pump driver.
// Also used for DIS_CENSTAR_7_DIGIT_CS (same data format).
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

bool longfeng8_get_event(nozzle_event_t *ne, uint8_t idx);
bool longfeng8_process_state_machine(const app_display_data_t *display_data, uint8_t nozzle_id, pumping_state_t *state);
bool longfeng8_process_data(app_display_data_t *display_data);
void longfeng8_data_validate(const app_display_data_t *display_data, uint8_t nozzle_id);

#ifdef __cplusplus
}
#endif
