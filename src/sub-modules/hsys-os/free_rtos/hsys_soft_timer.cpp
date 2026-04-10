

// hsys_soft_timer.cpp
#include "hsys_soft_timer.h"
#include <stdbool.h>

extern "C" {
	#include "freertos/FreeRTOS.h"
	#include "freertos/timers.h"
}

hsys_timer_handle_t hsys_timer_create(const char* timer_name, uint32_t period_ms, bool auto_reload, void* user_data, void (*callback)(void*)) {
    return (hsys_timer_handle_t)xTimerCreate(timer_name, pdMS_TO_TICKS(period_ms), auto_reload ? pdTRUE : pdFALSE, user_data, (TimerCallbackFunction_t)callback);
}

void * hsys_timer_get_userdata(hsys_timer_handle_t timer_handle) {
    return pvTimerGetTimerID((TimerHandle_t)timer_handle);
}

bool hsys_start_timer(hsys_timer_handle_t timer_handle) {
    if (timer_handle == NULL) {
        return false;
    }
    return xTimerStart((TimerHandle_t)timer_handle, 0) == pdPASS;
}

bool hsys_stop_timer(hsys_timer_handle_t timer_handle) {
    if (timer_handle == NULL) {
        return false;
    }
    return xTimerStop((TimerHandle_t)timer_handle, 0) == pdPASS;
}

bool hsys_reset_timer(hsys_timer_handle_t timer_handle) {
    if (timer_handle == NULL) {
        return false;
    }
    return xTimerReset((TimerHandle_t)timer_handle, 0) == pdPASS;
}

bool hsys_delete_timer(hsys_timer_handle_t timer_handle) {
    if (timer_handle == NULL) {
        return false;
    }
    return xTimerDelete((TimerHandle_t)timer_handle, 0) == pdPASS;
}

bool hsys_soft_timer_set_period(hsys_timer_handle_t timer_handle, uint32_t period_ms, uint32_t ticks_to_wait) {

    if (timer_handle == NULL) {
        return false;
    }
    return xTimerChangePeriod((TimerHandle_t)timer_handle, pdMS_TO_TICKS(period_ms), ticks_to_wait) == pdPASS;
}
