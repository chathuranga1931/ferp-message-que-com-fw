

#pragma once

#include "app_common.h"

typedef enum {

} app_hw_event_t;

typedef void (*fp_app_hw_on_event_t)(app_hw_event_t event, void * arg);

typedef struct {
    fp_app_hw_on_event_t fp_app_hw_on_event;
    app_init_t app_init;
}app_hw_init_t;

void app_hw_init(const app_hw_init_t * p_hw_init);
void app_hw_run();