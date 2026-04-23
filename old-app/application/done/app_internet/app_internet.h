#ifndef __APP_INTERNET_H__
#define __APP_INTERNET_H__

#include <stdint.h>
#include "app_common.h"


typedef enum{
    APP_INTERNET_EVENT_CONNECTED,
    APP_INTERNET_EVENT_DISCONNECTED,
}app_internet_event_t;

typedef void (*fp_app_internet_on_event_t)(app_internet_event_t event, void * arg);

// Internet initialization structure
typedef struct {
    fp_app_internet_on_event_t fp_app_internet_on_event;
    app_init_t app_init;
}app_internet_init_t;


// Function declarations
void app_internet_init(const app_internet_init_t * p_internet_init);
void app_internet_run(void);
int32_t app_internet_get_status(bool * connected);

#endif // __APP_INTERNET_H__
