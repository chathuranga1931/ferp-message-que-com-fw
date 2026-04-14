
// hsys_led.c
#include "hsys_led.h"
#include <string.h>

#include "pal_logger.h"

#define __TAG__  "HS_LED  "

#define HSYS_LED_DEBUG_LOG_EN LOG_DIS

static void hsys_led_timer_callback(void* user_data) {
    
    hsys_led_t* led = (hsys_led_t*)hsys_timer_get_userdata(user_data);
    if (!led) return;

    uint8_t step = led->current_step;
    uint8_t length = led->pattern_length;

    if (step < length && (led->repeat_count)) {
        // Calculate the bit to check based on the pattern length
        uint8_t bit_to_check = length - 1 - step;
        if (led->cue_pattern & (1U << bit_to_check)) {
            led->led_on();
        } else {
            led->led_off();
        }

        // Advance to the next step
        led->current_step++;

        if(led->current_step >= length){
            if (led->repeat_count == 0xFF) {
                // Repeat forever
                led->current_step = 0;
            } else if (led->repeat_counter < led->repeat_count) {
                // Repeat limited times
                led->current_step = 0;
                led->repeat_counter++;
            } else {
                // Stop the timer as repeat count is exhausted
                hsys_stop_timer(led->timer);
                led->led_off();
                led->repeat_count = 0;
                LOG_MSG_DEBUG(HSYS_LED_DEBUG_LOG_EN, "Pattern Stopped");
            }
        }
    }
}

bool hsys_led_init(hsys_led_t* led, void (*led_on)(void), void (*led_off)(void)) {

    LOG_MSG_DEBUG(HSYS_LED_DEBUG_LOG_EN, "Initializing LED...");

    if (!led || !led_on || !led_off) return false;

    memset(led, 0, sizeof(hsys_led_t));
    led->led_on = led_on;
    led->led_off = led_off;
    led->cue_pattern = 0;
    led->pattern_length = 0;
    led->repeat_count = 0;
    led->current_step = 0;
    led->repeat_counter = 0;

    led->timer = hsys_timer_create("LED Timer", CUE_RESOLUTION_MS, true, led, hsys_led_timer_callback);
    LOG_MSG_DEBUG(HSYS_LED_DEBUG_LOG_EN, "Creating LED timer %s", led->timer == NULL ? "Failed" : "Success");
    return led->timer != NULL;
}

bool hsys_led_set_pattern(hsys_led_t* led, uint32_t cue_pattern, uint8_t pattern_length, uint8_t repeat_count) {
    
    LOG_MSG_DEBUG(HSYS_LED_DEBUG_LOG_EN, "Setting LED pattern...");
    
    if (!led || pattern_length > 32 || pattern_length == 0) return false;
    led->cue_pattern = cue_pattern;
    led->pattern_length = pattern_length;
    led->repeat_count = repeat_count;
    led->current_step = 0;
    led->repeat_counter = 0;
    return true;
}

bool hsys_led_start(hsys_led_t* led) {
    LOG_MSG_DEBUG(HSYS_LED_DEBUG_LOG_EN, "Starting LED...");
    if (!led || !led->timer) return false;
    return hsys_start_timer(led->timer);
}

bool hsys_led_stop(hsys_led_t* led) {
    if (!led || !led->timer) return false;
    return hsys_stop_timer(led->timer);
}

bool hsys_led_reset(hsys_led_t* led) {
    if (!led || !led->timer) return false;
    led->current_step = 0;
    led->repeat_counter = 0;
    return hsys_reset_timer(led->timer);
}
