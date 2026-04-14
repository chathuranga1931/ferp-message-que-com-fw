// hsys_button.h
#ifndef HSYS_BUTTON_H
#define HSYS_BUTTON_H

#include "hsys_soft_timer.h"
#include "hsys_mutex.h"

#include <stdint.h>
#include <stdbool.h>

enum push_button_state_t{
    PUSH_BUTTON_STATE_UNKNOWN,
    PUSH_BUTTON_STATE_RELEASED,
    PUSH_BUTTON_STATE_PRESSED,
};

typedef struct {
    hsys_mutex_handle_t mutex;
    void (*on_short_press)(void);
    void (*on_long_press)(void);
    hsys_timer_handle_t timer;
    uint32_t press_start_time;
    push_button_state_t is_pressed;
    uint32_t short_press_duration_ms;
    uint32_t long_press_duration_ms;
} hsys_button_t;

bool hsys_button_init(hsys_button_t* button,
                      void (*on_short_press)(void),
                      void (*on_long_press)(void),
                      uint32_t short_press_duration_ms,
                      uint32_t long_press_duration_ms);
void hsys_button_press_event(void * arg, uint64_t timestamp_us);
void hsys_button_release_event(void * arg, uint64_t timestamp_us);

#endif // HSYS_BUTTON_H