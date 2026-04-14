#ifndef __FERP_CLIENT_H__
#define __FERP_CLIENT_H__

#include "error.h"
#include "device_config.h"
#include "nozzel_event.h"

ret_t ferp_client_init(device_configs_t * device_configs);
ret_t ferp_push_data(nozzel_event_t * n_event, uint8_t nozzel_id);
ret_t ferp_send_heart_beat();


#endif //__FERP_CLIENT_H__