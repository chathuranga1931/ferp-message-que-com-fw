// hsys_button.h
#ifndef HSYS_BUTTON_H
#define HSYS_BUTTON_H

#include "hsys_soft_timer.h"
#include "hsys_mutex.h"

#include <stdint.h>
#include <stdbool.h>

enum tog_button_state_t{
    TOG_BUTTON_STATE_UNKNOWN,
    TOG_BUTTON_STATE_RELEASED,
    TOG_BUTTON_STATE_PRESSED,
};

typedef struct {
    hsys_mutex_handle_t mutex;
    void (*on_press)(void);
    void (*on_release)(void);
    hsys_timer_handle_t timer;
    uint32_t press_start_time;
    tog_button_state_t is_pressed;
    uint32_t release_debounce_time_ms;
    uint32_t press_debounce_time_ms;
} hsys_tog_button_t;

bool hsys_tog_button_init(hsys_tog_button_t* button,
                      void (*on_press)(void),
                      void (*on_release)(void),
                      uint32_t release_debounce_time_ms,
                      uint32_t press_debounce_time_ms);
void hsys_tog_button_press_event(void * arg, uint64_t timestamp_us);
void hsys_tog_button_release_event(void * arg, uint64_t timestamp_us);

#endif // HSYS_BUTTON_H