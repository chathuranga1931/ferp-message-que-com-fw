

#pragma once

#include "app_common.h"


typedef enum{    

}app_buzzer_event_t;

typedef void (*fp_app_buzzer_on_event_t)(app_buzzer_event_t event, void * arg);

typedef struct {
    fp_app_buzzer_on_event_t fp_app_buzzer_on_event;
    app_init_t app_init;
}app_buzzer_init_t;

void app_buzzer_init(const app_buzzer_init_t * init);
void app_buzzer_run(void);
