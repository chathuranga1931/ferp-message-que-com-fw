
#include "app_default_btn.h"

#include "pal_logger.h"
#include "hsys_button.h"

#include "board.h"

#define __TAG__  "APP_DBTN"

static fp_app_default_btn_on_event_t _on_event;
static hsys_button_t _button_default;
static bool _is_initialized = false;
static fp_wake_task_t _wake;
static void * _wake_context;

void on_default_buttons_long_press();
void on_default_buttons_short_press();

void app_default_btn_init(const app_default_btn_init_t * p_default_btn_init)
{
    if(p_default_btn_init->fp_app_default_btn_on_event == NULL)
    {
        LOG_MSG_ERROR(LOG_EN, "Critical Error! : button default");
        while (1);
    }

    if(NULL == p_default_btn_init->app_init.fp_wake || NULL == p_default_btn_init->app_init.wake_context)
    {
        LOG_MSG_ERROR(LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    _on_event = p_default_btn_init->fp_app_default_btn_on_event;
    _wake = p_default_btn_init->app_init.fp_wake;
    _wake_context = p_default_btn_init->app_init.wake_context;

    hsys_button_init(&_button_default, 
        on_default_buttons_short_press,
        on_default_buttons_long_press, 
        200,
        5000
    );

    board_register_cb_on_button_default_press(hsys_button_press_event, (void *)&_button_default);
    board_register_cb_on_button_default_release(hsys_button_release_event, (void *)&_button_default);

    _is_initialized = true;
}

void on_default_buttons_short_press(){
    LOG_MSG_DEBUG(LOG_EN, "Default button short press");
    if(_on_event){
        _on_event(APP_DEFAULT_BTN_SHORT_PRESSED, nullptr);
    }
}

void on_default_buttons_long_press(){
    LOG_MSG_DEBUG(LOG_EN, "Default button long press");
    if(_on_event){
        _on_event(APP_DEFAULT_BTN_LONG_PRESSED, nullptr);
    }
}

void app_default_btn_run(){

}