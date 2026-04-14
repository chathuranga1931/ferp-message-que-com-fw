
#pragma once

#include "app_common.h"

typedef enum{
    APP_CLOUD_EVNT_NETWORK_CONFIG_READY,
    APP_CLOUD_EVNT_NETWORK_CONFIG_FAILED_RETRING,
    APP_CLOUD_EVNT_FUEL_PUMPED_FAILED,
    APP_CLOUD_EVNT_FUEL_PUMPED_SUCCESS,
}app_cloud_event_t;

typedef void (*fp_app_cloud_on_event_t)(app_cloud_event_t event, void * arg);

typedef struct {
    int32_t (*fp_on_cloud_register_rqst)(void * arg);
    int32_t (*fp_on_cloud_event_startup_rqst)(void * arg);
    int32_t (*fp_on_cloud_event_status_updated)(void * arg);
    int32_t (*fp_on_cloud_event_hb_rqst)(void * arg);
    int32_t (*fp_on_cloud_event_reconnect_rqst)(void * arg);
    int32_t (*fp_on_cloud_event_pumped_rqst)(void * arg);
    int32_t (*fp_on_cloud_event_printed_rqst)(void * arg);
    int32_t (*fp_get_device_uuid)(char * uuid_str, uint32_t max_len);
}cloud_driver_t;

typedef struct {
    fp_app_cloud_on_event_t fp_app_cloud_on_event;
    const cloud_driver_t * drv;
    app_init_t app_init;
}app_cloud_init_t;


void app_cloud_init(const app_cloud_init_t * p_cloud_init);
void app_cloud_run();
