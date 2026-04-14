#ifndef __PRINTER_H__
#define __PRINTER_H__

#include "error.h"
#include "device_config.h"
#include "que.h"
#include "print_event.h"

ret_t printer_init(device_configs_t * device_configs, que_t * ptint_que);
ret_t printer_start_server(que_t * ptint_que);
ret_t printer_server_init(device_configs_t * device_configs);
ret_t printer_print(print_event_t * pe);
void printer_keep_connected();

#endif //__PRINTER_H__