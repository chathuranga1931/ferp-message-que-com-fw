// hsys_soft_timer.h
#ifndef HSYS_SOFT_TIMER_H
#define HSYS_SOFT_TIMER_H

#include <stdint.h>
#include <stdbool.h>

// Abstract handle type to hide RTOS dependencies
typedef void* hsys_timer_handle_t;

// Create a timer
hsys_timer_handle_t hsys_timer_create(const char* timer_name, uint32_t period_ms, bool auto_reload, void* user_data, void (*callback)(void*));

// Start a timer
bool hsys_start_timer(hsys_timer_handle_t timer_handle);

// Stop a timer
bool hsys_stop_timer(hsys_timer_handle_t timer_handle);

// Reset a timer
bool hsys_reset_timer(hsys_timer_handle_t timer_handle);

// Delete a timer
bool hsys_delete_timer(hsys_timer_handle_t timer_handle);

void * hsys_timer_get_userdata(hsys_timer_handle_t timer_handle);

bool hsys_soft_timer_set_period(hsys_timer_handle_t timer_handle, uint32_t period_ms, uint32_t ticks_to_wait);

#endif // HSYS_SOFT_TIMER_H