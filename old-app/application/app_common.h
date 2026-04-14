

#pragma once

#include <stdint.h>

#define DEVICE_TYPE ("ferp-com")

#define ERROR_APP_NOT_INITIALIZED                   (1)
#define ERROR_APP_NULL                              (2)
#define ERROR_APP_BUSY                              (3)
#define ERROR_APP_SPIFF_NO_FILE                     (4)
#define ERROR_APP_CLOUD_INVALID_MAC_ADDRESS         (4)
#define ERROR_APP_CLOUD_NO_NONCE                    (5)
#define ERROR_APP_CLOUD_GET_AGENT_CONFIG_FAILED     (6)
#define ERROR_APP_CLOUD_GET_NOZZLE_CONFIG_FAILED    (7)
#define ERROR_APP_INVALID_JSON                      (8)
#define ERROR_APP_INVALID_ARGUMENTS                 (11)

typedef void (*fp_event_interface_t)(uint32_t event, void * arg);

typedef struct{
    fp_event_interface_t on_fuel_event;
    fp_event_interface_t on_ext_disptap_event;
    fp_event_interface_t on_wifi_event;
    fp_event_interface_t on_hw_event;
    fp_event_interface_t on_config_event;
    fp_event_interface_t on_ota_event;
    fp_event_interface_t on_cloud_event;
    fp_event_interface_t on_logging_event;
    fp_event_interface_t on_led_event;
    fp_event_interface_t on_printer_event;
    fp_event_interface_t on_print_btn_event;
    fp_event_interface_t on_default_btn_event;
    fp_event_interface_t on_buzzer_event;
    fp_event_interface_t on_timing_event;
    fp_event_interface_t on_screen_event;
    fp_event_interface_t on_retransmission_event;
    fp_event_interface_t on_spiffs_event;
    fp_event_interface_t on_sd_event;
    fp_event_interface_t on_webserver_event;
    fp_event_interface_t on_sys_event;
    fp_event_interface_t on_mqtt_event;
    fp_event_interface_t on_internet_event;
    fp_event_interface_t on_timeman_event;
}event_table_t;

typedef void (*fp_wake_task_t)(void * arg);

typedef struct {
    event_table_t * event_table;
    fp_wake_task_t fp_wake;
    void * wake_context;
}app_init_t;


typedef struct{
    uint8_t fuel_type;
    unsigned long time_updated;
    uint64_t unit_pricex100;
    uint64_t total_pricex100;
    uint64_t volume_lx1000;
    bool start_stop;
    bool select_p;
    bool select_l;
} app_display_data_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    struct{
        struct 
        {
            uint8_t start_stop : 1;
            uint8_t select_p : 1;
            uint8_t select_l : 1;
            uint8_t select_ll : 1;
            uint8_t rest : 4;
        }flags;
        union
        {
            struct
            {
                uint8_t index : 1;				// mark error if index is not matching
                uint8_t unitprice : 1; 			// mark error if unit price indexes are not matching
                uint8_t totprice : 1;			// mark error if total price indexes are not matching
                uint8_t volume : 1;				// mark error if volume indexes are not matching
                uint8_t price_gap : 1;		    // mark error if price volume has a gap
                uint8_t rest : 3;
            }error;
            uint8_t errors;
        };	
        union
        {
            struct
            {
                uint32_t total_price;
                uint32_t volume_l;
            };
            uint64_t total_liters;
        };
        uint32_t unit_price;  
    }data;
} app_disptap_display_data_t;

