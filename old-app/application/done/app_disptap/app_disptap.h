
#pragma once

#include "app_common.h"


typedef enum{
    APP_ESP07_EVENT_FW_VERSION_LOADED,
    APP_DISPTAP_EVENT_DISPLAY1_DATA_READY,
    APP_DISPTAP_EVENT_DISPLAY2_DATA_READY,
}app_disptap_event_t;

typedef void (*fp_app_disptap_on_event_t)(app_disptap_event_t event, void * arg);

typedef struct {
    fp_app_disptap_on_event_t fp_app_disptap_on_event;
    app_init_t app_init;
}app_disptap_init_t;

void app_disptap_init(const app_disptap_init_t * p_disptap_init);
void app_disptap_run();