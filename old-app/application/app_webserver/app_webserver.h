
#pragma once

#include "app.h"
#include "app_common.h"

#include "pal_spiffs.h"
#include "pal_http_server.h"

typedef enum {
    APP_WEB_SERVER_EVENT_NONE = 0,
    APP_WEB_SERVER_EVENT_STARTED,
    APP_WEB_SERVER_EVENT_CONFIG_UPDATED,
}app_webserver_event_t;

typedef void (*fp_app_webserver_on_event_t)(app_webserver_event_t event, void * arg);


typedef struct {
    int32_t (*fp_on_esp32bin_file_received)(const char * filename, size_t index, uint8_t *data, size_t len, bool final);
    const char * (*fp_on_esp32bin_file_get_status_string)(void);
    int32_t (*fp_on_esp07bin_file_received)(const char * filename, size_t index, uint8_t *data, size_t len, bool final);
    const char * (*fp_on_esp07bin_file_get_status_string)(void);
}web_server_cb_t;

typedef struct {    
    fp_app_webserver_on_event_t fp_app_webserver_on_event;
    web_server_cb_t * cb_table;
    app_init_t app_init;
}app_webserver_init_t;


void app_webserver_init(const app_webserver_init_t * p_webserver_init);
void app_webserver_run();