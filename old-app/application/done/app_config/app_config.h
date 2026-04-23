
#pragma once

#include "app_common.h"
#include "app_wifi.h"

#include "pal_mqtt.h"

#include "hsys_config.h"

#include <stdint.h>


typedef struct{
    char nozzel_id_for_print[SIZE_OF_NOZZELID];
    char fuel_type_str_for_print[SIZE_OF_FUEL_TYPE_STR]; 
}app_nozzle_init_t;

typedef struct {    
	char uuid[SIZE_OF_UUID]; // = "3df3176c-9d64-4b48-8651-5ef503ebaabb";
	char nozzle_id[SIZE_OF_NOZZELID];
	char fuel_type[SIZE_OF_FUEL_TYPE];
	char fuel_type_str[SIZE_OF_FUEL_TYPE_STR];
}app_fix_nozzle_config_t;

typedef struct{    
    char device_group[SIZE_OF_DEVICE_GROUP];
    bool enable_hb = true;
	uint32_t hb_interval_s = 60; 
    uint32_t hb_interval_secs;
    uint32_t display_type;
    uint32_t dt_log_rate = 0;
    uint32_t stabilize_delay_ms = 1000;
    char printer_url[SIZE_OF_NTWK_BASE_URL];  
    app_nozzle_init_t nozzle[NO_NOZZELS];     
    bool enable_nid_print = false; 
    bool enable_nid_cloud = false;
    char url[SIZE_OF_NTWK_BASE_URL]; // = "https://http-ingress-alw5epn3aq-el.a.run.app/api/v1/device/data";
	char secret[SIZE_OF_SECRET];
	// char agent_uuid[SIZE_OF_UUID];
	char basic_authentication_base64[SIZE_OF_SECRET];
    app_fix_nozzle_config_t fix_nzzle[NO_NOZZELS];  
}app_settings_init_t;


typedef struct {
	char url [SIZE_OF_NTWK_BASE_URL]; //"http://192.168.1.5/print";	//"http://ferp-iot-printer12121212"; hostname of the device
	uint32_t print_copy_count = DEFAULT_PRINT_COPY_COUNT;
	uint32_t print_delay_ms = DEFAULT_PRINTER_PRINT_DELAY_MS;
}printer_configs_t;

typedef struct{
    bool serial_log_enabled;
    uint32_t serial_log_level;
    bool udp_log_enabled; 
    uint32_t udp_log_level;
    char udp_server_ip[SIZE_OF_IP_ADDRESS];
    uint32_t udp_server_port;
}app_logging_t;

typedef struct {
    pal_wifi_init_config_t wifi;
    app_settings_init_t app;
    printer_configs_t printer;
    pal_mqtt_config_t mqtt;
    app_logging_t logging;
    char group[SIZE_OF_DEVICE_GROUP];
}app_config_t; 

typedef enum{
    APP_CONFIG_EVENT_LOADED,

}app_config_event_t;

typedef void (*fp_app_config_on_event_t)(app_config_event_t event, void * arg);

typedef struct {
    fp_app_config_on_event_t fp_app_config_on_event;
    config_t * config_table;
    uint16_t config_table_size;
    app_init_t app_init;
}app_config_init_t;



void app_config_init(const app_config_init_t * p_config_init);
void app_config_run();

int32_t app_config_get_config_json(char * config_json, uint32_t * config_size, uint32_t timeout_ms);
int32_t app_config_set_config_json(char * config_json, uint32_t config_size, uint32_t timeout_ms);
// int32_t app_config_get_mqtt_init(hsys_mqtt_init_t * mqtt_init, uint32_t timeout_ms);
int32_t app_config_get_app_settings(app_settings_init_t * app_settings_init, uint32_t timeout_ms);
// int32_t app_config_get_device(app_device_t * app_device, uint32_t timeout_ms);
int32_t app_config_get_mac_str(char * mac, uint32_t timeout_ms);
int32_t app_config_get_uuid(char * uuid, uint32_t timeout_ms);
int32_t app_config_get_display_type(uint32_t * display_type, uint32_t timeout_ms);

int32_t app_config_get_config(const char * config_name, void * config_value, uint32_t timeout_ms);

int32_t app_config_set(const char * config_name, const void * config_value, uint16_t bytes, hsys_type_t type, uint32_t timeout_ms);
int32_t app_config_get(const char * config_name, void * config_value, uint16_t * bytes, hsys_type_t * type, uint32_t timeout_ms);

