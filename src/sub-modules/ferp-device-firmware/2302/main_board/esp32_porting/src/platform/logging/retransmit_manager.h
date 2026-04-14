#ifndef _RETRANSMIT_MANGER__H_
#define _RETRANSMIT_MANGER__H_

#include <Arduino.h>

#include "error.h"
#include "device_config.h"

ret_t add_event_to_retransmit_list(nozzel_event_t * ne, uint8_t nozzel_id);
ret_t get_event_from_retransmit_list(nozzel_event_t * ne, uint8_t nozzel_id);
ret_t update_dates(uint8_t nozzel_id);

/*
 * Once this is called, the list maintained will be updated and write to
 * status file.
 */
ret_t on_retransmit_success(uint8_t nozzel_id);
ret_t retransmit_manger_init(device_configs_t * device_configurations);

#endif // _RETRANSMIT_MANGER__H_