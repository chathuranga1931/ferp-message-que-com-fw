
#pragma once

#include "app_common.h"

typedef enum {
    APP_DEFAULT_BTN_SHORT_PRESSED,
    APP_DEFAULT_BTN_LONG_PRESSED,
}app_default_btn_event_t;

typedef void (*fp_app_default_btn_on_event_t)(app_default_btn_event_t event, void * arg);


typedef struct {
    fp_app_default_btn_on_event_t fp_app_default_btn_on_event;
    app_init_t app_init;
}app_default_btn_init_t;

void app_default_btn_init(const app_default_btn_init_t * p_default_btn_init); 

void app_default_btn_run();