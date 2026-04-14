// hsys_buz.h
#ifndef HSYS_BUZZER_H
#define HSYS_BUZZER_H

#include <stdint.h>
#include <stdbool.h>
#include "hsys_soft_timer.h"

#define MAX_BUZ 5
#define CUE_RESOLUTION_MS 250

typedef struct {
    void (*buz_on)(void);
    void (*buz_off)(void);
    uint32_t cue_pattern;
    uint8_t pattern_length;
    uint8_t repeat_count;
    uint8_t current_step;
    uint8_t repeat_counter;
    hsys_timer_handle_t timer;
} hsys_buz_t;

bool hsys_buz_init(hsys_buz_t* buz, void (*buz_on)(void), void (*buz_off)(void));
bool hsys_buz_set_pattern(hsys_buz_t* buz, uint32_t cue_pattern, uint8_t pattern_length, uint8_t repeat_count);
bool hsys_buz_start(hsys_buz_t* buz);
bool hsys_buz_stop(hsys_buz_t* buz);
bool hsys_buz_reset(hsys_buz_t* buz);

#endif  // HSYS_BUZZER_H