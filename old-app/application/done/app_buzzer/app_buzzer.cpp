
#include "app.h"

#include "pal_logger.h"
#include "hsys_buzzer.h"

#include "app_buzzer.h"
#include "app_print_btn.h"

#include "board.h"

#define __TAG__  "APP_BZR "

static fp_app_buzzer_on_event_t _on_event;
static hsys_timer_handle_t _buzzer_timer;
static hsys_buz_t _buzzer;
static bool _is_initialized = false;

static void _on_print_button(app_print_btn_event_t event, void * arg);
static void _on_system_event(app_sys_event_t event, void * arg);

void app_buzzer_init(const app_buzzer_init_t * init){

    if(init->fp_app_buzzer_on_event == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error! buzzer init");
        while(1);
    }
    _on_event = init->fp_app_buzzer_on_event;

    hsys_buz_init(&_buzzer, board_buz_on, board_buz_off);
    // hsys_buz_set_pattern(&_buzzer, 0b11110000, 8, 3);
    // hsys_buz_start(&_buzzer);

    init->app_init.event_table->on_print_btn_event = (fp_event_interface_t)_on_print_button;
    init->app_init.event_table->on_sys_event = (fp_event_interface_t)_on_system_event;

    _is_initialized = true;
}

static void _on_print_button(app_print_btn_event_t event, void * arg){
    switch(event){
        case APP_PRINT1_BUTTON_SORT_PRESSED:
            hsys_buz_set_pattern(&_buzzer, 0b11111111, 8, 1);
            break;
        default:
            break;
    }
}

static void _on_system_event(app_sys_event_t event, void * arg){
    switch(event){
        case APP_SYS_EVENT_REBOOT_SCHEDULED_IN_X_SECONDS:
            hsys_buz_set_pattern(&_buzzer, 0b100, 3, 1);
            break;
        default:
            break;
    }
}

void app_buzzer_run(void){
    
}