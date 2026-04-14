#ifndef __DISPLAY1_DECODER_H__
#define __DISPLAY1_DECODER_H__

#include "error.h"
#include "device_config.h"

void display_decoder_init(device_configs_t * device_configs);
void display_decoder_process(display_data_t display_data, uint8_t nozzel_id, que_t * n_event_que, bool * prev_start_stop);

#endif //__DISPLAY1_DECODER_H__