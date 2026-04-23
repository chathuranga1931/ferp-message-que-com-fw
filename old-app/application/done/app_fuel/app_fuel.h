
#pragma once

#include "app_common.h"

typedef enum{
    APP_FUEL_EVENT_PUMPING_STARTED,
    APP_FUEL_EVENT_PUMPING_STOPPED,
    APP_FUEL_EVENT_PUMPED,
}app_fuel_event_t;

typedef void (*fp_app_fuel_on_event_t)(app_fuel_event_t event, void * arg);

typedef struct {
    fp_app_fuel_on_event_t fp_app_fuel_on_event;
    app_init_t app_init;
}app_fuel_init_t;

void app_fuel_init(const app_fuel_init_t * p_fuel_init);
void app_fuel_run();

