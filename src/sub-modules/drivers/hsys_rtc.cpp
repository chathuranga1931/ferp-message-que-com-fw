
#include <stdbool.h>
#include "hsys_rtc.h"

bool ds1307_is_running_wrapper(void) {
    return ds1307_is_running();
}

bool ds1307_read_time_wrapper(time_t * time_rtc) {
    return ds1307_read_time(time_rtc) == ERROR_DS1307_OK;
}

bool ds1307_set_time_wrapper(time_t time_rtc) {
    return ds1307_set_time(time_rtc) == ERROR_DS1307_OK;
}