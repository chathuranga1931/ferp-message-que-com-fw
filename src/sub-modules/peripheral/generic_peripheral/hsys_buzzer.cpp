
// hsys_buz.c
#include "hsys_buzzer.h"
#include <string.h>

#include "pal_logger.h"

#define __TAG__  "HS_BUZ  "

static void hsys_buz_timer_callback(void* user_data) {
    
    hsys_buz_t* buz = (hsys_buz_t *)hsys_timer_get_userdata(user_data);
    if (!buz) return;

    uint8_t step = buz->current_step;
    uint8_t length = buz->pattern_length;

    if (step < length && (buz->repeat_count)) {
        // Calculate the bit to check based on the pattern length
        uint8_t bit_to_check = length - 1 - step;
        if (buz->cue_pattern & (1U << bit_to_check)) {
            buz->buz_on();
        } else {
            buz->buz_off();
        }

        // Advance to the next step
        buz->current_step++;

        if(buz->current_step >= length){
            if (buz->repeat_count == 0xFF) {
                // Repeat forever
                buz->current_step = 0;
            } else if (buz->repeat_counter < buz->repeat_count) {
                // Repeat limited times
                buz->current_step = 0;
                buz->repeat_counter++;
            } else {
                // Stop the timer as repeat count is exhausted
                hsys_stop_timer(buz->timer);
                buz->buz_off();
                buz->repeat_count = 0;
                LOG_MSG_DEBUG(LOG_EN, "Pattern Stopped");
            }
        }
    }
}

bool hsys_buz_init(hsys_buz_t* buz, void (*buz_on)(void), void (*buz_off)(void)) {

    LOG_MSG_DEBUG(LOG_EN, "Initializing LED...");

    if (!buz || !buz_on || !buz_off) return false;

    memset(buz, 0, sizeof(hsys_buz_t));
    buz->buz_on = buz_on;
    buz->buz_off = buz_off;
    buz->cue_pattern = 0;
    buz->pattern_length = 0;
    buz->repeat_count = 0;
    buz->current_step = 0;
    buz->repeat_counter = 0;

    // LOG_MSG_DEBUG(LOG_EN, "Creating LED timer B..." + std::to_string((uint32_t)buz->timer));
    buz->timer = hsys_timer_create("LED Timer", CUE_RESOLUTION_MS, true, buz, hsys_buz_timer_callback);
    // LOG_MSG_DEBUG(LOG_EN, "Creating LED timer A..." + std::to_string((uint32_t)buz->timer));
    return buz->timer != NULL;
}

bool hsys_buz_set_pattern(hsys_buz_t* buz, uint32_t cue_pattern, uint8_t pattern_length, uint8_t repeat_count) {
    
    LOG_MSG_DEBUG(LOG_EN, "Setting Buzzer pattern...");
    
    if (!buz || pattern_length > 32 || pattern_length == 0) return false;
    buz->cue_pattern = cue_pattern;
    buz->pattern_length = pattern_length;
    buz->repeat_count = repeat_count;
    buz->current_step = 0;
    buz->repeat_counter = 0;
    return true;
}

bool hsys_buz_start(hsys_buz_t* buz) {
    LOG_MSG_DEBUG(LOG_EN, "Starting LED...");
    if (!buz || !buz->timer) return false;
    return hsys_start_timer(buz->timer);
}

bool hsys_buz_stop(hsys_buz_t* buz) {
    if (!buz || !buz->timer) return false;
    return hsys_stop_timer(buz->timer);
}

bool hsys_buz_reset(hsys_buz_t* buz) {
    if (!buz || !buz->timer) return false;
    buz->current_step = 0;
    buz->repeat_counter = 0;
    return hsys_reset_timer(buz->timer);
}
