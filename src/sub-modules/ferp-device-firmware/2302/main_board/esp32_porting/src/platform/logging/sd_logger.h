
#ifndef _SD_LOGGER__H_
#define _SD_LOGGER__H_

#include "device_config.h"
#include "error.h"

ret_t sd_logger_init(device_configs_t * device_configs);
ret_t create_log_file();
void write_log(String log_message);
void write_note(String log_message);
void write_error(String log_message);
void write_pumped_event_log(String log_message, uint8_t nozzel_id);
void write_cloud_failed_event_log(String log_message, uint8_t nozzel_id);
void write_buffer_log(unsigned char * BuffA, unsigned char * BuffB, unsigned int length);
void write_debug_log(String msg);
void write_display_tap_data(String data);

#endif