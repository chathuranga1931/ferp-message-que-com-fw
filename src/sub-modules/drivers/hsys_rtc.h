
#ifndef RTC_HPP
#define RTC_HPP

#include <stdint.h>
#include <time.h>
#include <stdbool.h>

#include "DS1307/ds1307.hpp"

bool ds1307_is_running_wrapper(void);
bool ds1307_read_time_wrapper(time_t * time_rtc);
bool ds1307_set_time_wrapper(time_t time_rtc);

typedef struct {
    bool (*fp_rtc_is_running)(void);
    bool (*fp_rtc_read_time)(time_t * time_rtc);
    bool (*fp_rtc_set_time)(time_t time_rtc);
}hsys_rtc_t;

const static hsys_rtc_t ds1307_rtc = {
    .fp_rtc_is_running = ds1307_is_running_wrapper,
    .fp_rtc_read_time = ds1307_read_time_wrapper,
    .fp_rtc_set_time = ds1307_set_time_wrapper
};

#endif // RTC_HPP