
#pragma once

#include "app_common.h"

typedef enum {
     /* Add more events as needed */
}app_led_event_t;

typedef void (*fp_app_led_on_event_t)(app_led_event_t event, void * arg);

typedef struct {
    fp_app_led_on_event_t fp_app_led_on_event;
    app_init_t app_init;
}app_led_init_t;

void app_led_init(const app_led_init_t * init);
void app_led_run(void);