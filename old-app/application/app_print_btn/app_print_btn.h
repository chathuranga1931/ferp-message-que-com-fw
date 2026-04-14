
#pragma once

#include "app_common.h"


typedef enum {
    APP_PRINT1_BUTTON_SORT_PRESSED,
    APP_PRINT1_BUTTON_LONG_PRESSED,
    APP_PRINT2_BUTTON_SORT_PRESSED,
    APP_PRINT2_BUTTON_LONG_PRESSED
}app_print_btn_event_t;

typedef void (*fp_app_print_btn_on_event_t)(app_print_btn_event_t event, void * arg);

typedef struct {
    fp_app_print_btn_on_event_t fp_app_print_btn_on_event;
    app_init_t app_init;
}app_print_btn_init_t;



void app_print_btn_init(const app_print_btn_init_t * p_print_btn_init); 
void app_print_btn_run();