
#ifndef __SANKI_6_DIGIT_1_H
#define __SANKI_6_DIGIT_1_H

#include <stdint.h>
#include "../nozzle_event.h"

bool sanki6_get_event(nozzle_event_t * ne, uint8_t idx);
bool sanki6_process_state_machine(const app_display_data_t * display_data, uint8_t nozzle_id);
bool sanki6_process_data(app_display_data_t * display_data);
void sanki6_data_validate(const app_display_data_t * display_data, uint8_t nozzle_id);

#endif