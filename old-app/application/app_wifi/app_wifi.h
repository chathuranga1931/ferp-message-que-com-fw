

#pragma once

#include "app_common.h"
#include "pal_wifi.h"

typedef enum{
    // WiFi events from PAL layer (aligned with pal_wifi_event_t)
    APP_WIFI_EVENT_STA_START = 0,
    APP_WIFI_EVENT_STA_CONNECTED,
    APP_WIFI_EVENT_STA_DISCONNECTED,
    APP_WIFI_EVENT_STA_GOT_IP,
    APP_WIFI_EVENT_AP_START,
    APP_WIFI_EVENT_AP_STOP,
    APP_WIFI_EVENT_AP_STACONNECTED,
    APP_WIFI_EVENT_AP_STADISCONNECTED,
    APP_WIFI_EVENT_STA_RSSI_CHANGED,
    
    // Application-specific WiFi events
    APP_WIFI_EVENT_NO_AVAILABLE_SIGNAL,
}app_wifi_event_t;


typedef void (*fp_app_wifi_on_event_t)(app_wifi_event_t event, void * arg);

typedef struct {
    fp_app_wifi_on_event_t fp_app_wifi_on_event;
    int32_t (*fp_app_wifi_get_init_config)(pal_wifi_init_config_t * wifi_init, uint32_t timeout_ms);
    app_init_t app_init;
}app_wifi_init_t;


void app_wifi_init(const app_wifi_init_t * p_wifi_init);
void app_wifi_run();

