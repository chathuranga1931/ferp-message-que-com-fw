
#ifndef DS1307_HPP
#define DS1307_HPP

#include <stdint.h>
#include <time.h>

#define ERROR_DS1307_OK                 0
#define ERROR_DS1307_INIT_FAILED       -1
#define ERROR_DS1307_READ_TIME_FAILED  -2
#define ERROR_DS1307_BATTERY_DEAD      -3

typedef struct{

}ds1307_init_t;

typedef struct{

}ds1307_handle_t;

bool ds1307_is_running(void);
int32_t ds1307_init(ds1307_init_t * init, ds1307_handle_t * handle);
int32_t ds1307_read_time(time_t * time_rtc);
int32_t ds1307_set_time(time_t time_rtc);

#endif // DS1307_HPP