#ifndef __DATE_TIME_MANAGER_H__
#define __DATE_TIME_MANAGER_H__

#include "error.h"
#include "device_config.h"

ret_t date_time_init(device_configs_t * device_configs);
ret_t date_time_sync(void);
ret_t get_time(struct tm * timeinfo);

#endif //__DATE_TIME_MANAGER_H__