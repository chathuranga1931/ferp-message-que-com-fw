
// hsys_button.c
#include "hsys_tog_button.h"
#include "hsys_soft_timer.h"
#include "hsys_mutex.h"
#include <string.h>
#include <time.h>

#include "pal_logger.h"

#define __TAG__  "HS_TGBTN"

static uint32_t get_current_time_ms() {
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return (spec.tv_sec * 1000) + (spec.tv_nsec / 1000000);
}

static void hsys_button_timer_callback(hsys_timer_handle_t user_data) {
    
    hsys_tog_button_t * button = (hsys_tog_button_t*)hsys_timer_get_userdata(user_data);

    if (!button || !button->mutex) return;

    if(hsys_mutex_try_lock(button->mutex, 0)){
        uint32_t elapsed_time = get_current_time_ms() - button->press_start_time;
        if(button->is_pressed == TOG_BUTTON_STATE_PRESSED){
            if (elapsed_time >= button->press_debounce_time_ms) {
                if (button->on_press) {
                    button->on_press();
                    // LOG_MSG_DEBUG(LOG_EN, "TOG BUTTON PRESSED");
                }
                hsys_stop_timer(button->timer); // Stop timer to avoid multiple triggers
            }
        }else{
            if (elapsed_time >= button->release_debounce_time_ms) {
                if (button->on_release) {
                    button->on_release();
                    // LOG_MSG_DEBUG(LOG_EN, "TOG BUTTON RELEASED");
                }
                hsys_stop_timer(button->timer); // Stop timer to avoid multiple triggers
            }
        }
        hsys_mutex_unlock(button->mutex);
    }    
}

bool hsys_tog_button_init(hsys_tog_button_t* button,
                            void (*on_press)(void),
                            void (*on_release)(void),
                            uint32_t release_debounce_time_ms,
                            uint32_t press_debounce_time_ms) {
    
    if (!button) return false;

    memset((void *)button, 0, sizeof(hsys_tog_button_t));
    button->on_press = on_press;
    button->on_release = on_release;
    button->release_debounce_time_ms = release_debounce_time_ms;
    button->press_debounce_time_ms = press_debounce_time_ms;
    button->press_start_time = 0;
    button->is_pressed = TOG_BUTTON_STATE_UNKNOWN;
    button->mutex = hsys_mutex_create();  
    // button->id = button_count;
    button->timer = hsys_timer_create("Button Timer", 100, true, (void *)button, hsys_button_timer_callback);

    // hsys_buttons[button_count++] = button;
    // LOG_MSG_DEBUG(LOG_EN, "Creating button " + std::to_string((uint32_t)button) + " " + std::to_string((uint32_t)button->timer));
    
    return button->timer != NULL;
}

void hsys_tog_button_press_event(void * arg, uint64_t timestamp_us) {

    hsys_tog_button_t* button = (hsys_tog_button_t*)arg;
    if (!button || (button->is_pressed == TOG_BUTTON_STATE_PRESSED)) return;

    if(hsys_mutex_try_lock(button->mutex, 0)){

        button->is_pressed = TOG_BUTTON_STATE_PRESSED;
        button->press_start_time = timestamp_us/1000.0;

        hsys_start_timer(button->timer);
        hsys_mutex_unlock(button->mutex);
    }
}

void hsys_tog_button_release_event(void * arg, uint64_t timestamp_us) {
    
    hsys_tog_button_t* button = (hsys_tog_button_t*)arg;
    if (!button || (button->is_pressed == TOG_BUTTON_STATE_RELEASED)) return;
    
    if(hsys_mutex_try_lock(button->mutex, 0)){

        button->is_pressed = TOG_BUTTON_STATE_RELEASED;
        button->press_start_time = timestamp_us/1000.0;

        hsys_start_timer(button->timer);
        hsys_mutex_unlock(button->mutex);
    }
}