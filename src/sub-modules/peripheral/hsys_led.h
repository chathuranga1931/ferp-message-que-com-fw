// hsys_led.h
#ifndef HSYS_LED_H
#define HSYS_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "hsys_soft_timer.h"

#define MAX_LEDS 5
#define CUE_RESOLUTION_MS 125

typedef struct {
    void (*led_on)(void);
    void (*led_off)(void);
    uint32_t cue_pattern;
    uint8_t pattern_length;
    uint8_t repeat_count;
    uint8_t current_step;
    uint8_t repeat_counter;
    hsys_timer_handle_t timer;
} hsys_led_t;

bool hsys_led_init(hsys_led_t* led, void (*led_on)(void), void (*led_off)(void));
bool hsys_led_set_pattern(hsys_led_t* led, uint32_t cue_pattern, uint8_t pattern_length, uint8_t repeat_count);
bool hsys_led_start(hsys_led_t* led);
bool hsys_led_stop(hsys_led_t* led);
bool hsys_led_reset(hsys_led_t* led);

#endif  // HSYS_LED_H