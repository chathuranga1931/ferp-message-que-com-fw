
#include "app.h"

#include "pal_logger.h"
#include "hsys_led.h"

#include "app_leds.h"

#include "board.h"

#define __TAG__  "APP_LED "

static fp_app_led_on_event_t _on_event;
static hsys_timer_handle_t _led_timer;
static hsys_led_t _led1;
static hsys_led_t _led2;
static bool _is_initialized = false;

static void _on_system_event(app_sys_event_t event, void * arg);

void app_led_init(const app_led_init_t * init){

    if(init->fp_app_led_on_event == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error! led init");
        while(1);
    }
    _on_event = init->fp_app_led_on_event;

    LOG_MSG_DEBUG(LOG_EN, "Initializing LEDs..."); 

    hsys_led_init(&_led1, board_led1_on, board_led1_off);
    hsys_led_init(&_led2, board_led2_on, board_led2_off);

    hsys_led_set_pattern(&_led1, 0b00001111, 8, 0xFF);
    //hsys_led_set_pattern(&_led2, 0b11110000, 8, 0xFF);
    hsys_led_start(&_led1);

    init->app_init.event_table->on_sys_event = (fp_event_interface_t)_on_system_event;

    _is_initialized = true;
}

static void _on_system_event(app_sys_event_t event, void * arg){
    switch(event){
        case APP_SYS_EVENT_REBOOT_SCHEDULED_IN_X_SECONDS:
            hsys_led_set_pattern(&_led1, 0b100, 3, 0xFF);
            break;
        default:
            break;
    }
}

void app_led_run(void){
    
}