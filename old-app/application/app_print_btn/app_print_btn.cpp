
#include "app_print_btn.h"

#include "pal_logger.h"
#include "hsys_button.h"

#include "board.h"

#define __TAG__  "APP_PBTN"

static fp_app_print_btn_on_event_t _on_event;
static hsys_button_t _button_printer1;
static hsys_button_t _button_printer2;
static bool _is_initialized = false;
static fp_wake_task_t _wake;
static void * _wake_context;

void on_printer1_buttons_long_press();
void on_printer2_buttons_long_press();
void on_printer1_buttons_short_press();
void on_printer2_buttons_short_press();

// void app_print_btn_init(fp_app_print_btn_on_event_t fp_app_default_btn_on_event, event_table_t * event_table, fp_wake_task_t fp_wake, void * wake_context){
void app_print_btn_init(const app_print_btn_init_t * p_print_btn_init){

    if(p_print_btn_init->fp_app_print_btn_on_event == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error! : button default");
    }
    _on_event = p_print_btn_init->fp_app_print_btn_on_event;
    
    if(NULL == p_print_btn_init->app_init.fp_wake || NULL == p_print_btn_init->app_init.wake_context){
        LOG_MSG_ERROR(LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    _wake = p_print_btn_init->app_init.fp_wake;
    _wake_context = p_print_btn_init->app_init.wake_context;

    hsys_button_init(&_button_printer1, 
        on_printer1_buttons_short_press,
        on_printer1_buttons_long_press, 
        50,
        5000
    );

    hsys_button_init(&_button_printer2, 
        on_printer2_buttons_short_press,
        on_printer2_buttons_long_press, 
        50,
        5000
    );

    board_register_cb_on_button_print1_press(hsys_button_press_event, (void *)&_button_printer1);
    board_register_cb_on_button_print1_release(hsys_button_release_event, (void *)&_button_printer1);
    board_register_cb_on_button_print2_press(hsys_button_press_event, (void *)&_button_printer2);
    board_register_cb_on_button_print2_release(hsys_button_release_event, (void *)&_button_printer2);

    _is_initialized = true;
}

void on_printer1_buttons_short_press(){
    LOG_MSG_DEBUG(LOG_EN, "Printer 1 button short press");
    if(_on_event){
        _on_event(APP_PRINT1_BUTTON_SORT_PRESSED, nullptr);
    }
}

void on_printer1_buttons_long_press(){
    LOG_MSG_DEBUG(LOG_EN, "Printer 1 button long press");
    if(_on_event){
        _on_event(APP_PRINT1_BUTTON_LONG_PRESSED, nullptr);
    }
}

void on_printer2_buttons_short_press(){
    LOG_MSG_DEBUG(LOG_EN, "Printer 2 button short press");
    if(_on_event){
        _on_event(APP_PRINT2_BUTTON_SORT_PRESSED, nullptr);
    }
}

void on_printer2_buttons_long_press(){
    LOG_MSG_DEBUG(LOG_EN, "Printer 2 button long press");
    if(_on_event){
        _on_event(APP_PRINT1_BUTTON_LONG_PRESSED, nullptr);
    }
}

void app_print_btn_run(){

}