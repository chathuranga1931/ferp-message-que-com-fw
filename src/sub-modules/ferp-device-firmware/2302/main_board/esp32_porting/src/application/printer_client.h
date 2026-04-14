#ifndef __PRINTER_CLIENT_H__
#define __PRINTER_CLIENT_H__

#include "error.h"
#include "device_config.h"

ret_t printer_client_init(device_configs_t * device_configs);
ret_t printer_push_data(nozzel_event_t * n_event, uint8_t nozzel_id);

#endif //__PRINTER_CLIENT_H__