

#include "app_common.h"
#include "app_cloud.h"
#include "app_wifi.h"
#include "app_internet.h"

#include "app.h"

#include "hsys_config.h"
#include "pal_logger.h"
#include "hsys_task.h"
#include "hsys_event.h"
#include "pal_wifi.h"
#include "hsys_soft_timer.h"
#include "hsys_queue.h"

#include "utils/utils.hpp"

#include "board.h"
// #include "cube_sphere_api.h"

#include "app_fuel.h"
#include "app_disptap.h"

// This should remove, and modify to independent on this type
#include "app_fuel/pumps/nozzle_event.h"

#define __TAG__  "APP_CLD "

#define CLOUD_DEBUG_LOG_EN      LOG_EN
#define CLOUD_WARN_LOG_EN       LOG_DIS
#define CLOUD_ERROR_LOG_EN      LOG_DIS
#define CLOUD_INFO_LOG_EN       LOG_DIS

#define WAKE_ME() if(_wake){_wake(_wake_context); }   

#define APP_CLOUD_EVENTS_WIFI_CONNECTED                 (0x1 << 0)
#define APP_CLOUD_EVENTS_INTERNET_CONNECTED             (0x1 << 1)
#define APP_CLOUD_EVENTS_STARTUP                        (0x1 << 2)
#define APP_CLOUD_EVENTS_FUEL_PUMPED                    (0x1 << 3)
#define APP_CLOUD_EVENTS_PRINTED                        (0x1 << 4)
#define APP_CLOUD_EVENTS_RECONNECT                      (0x1 << 5)
#define APP_CLOUD_EVENTS_HEARTBEAT                      (0x1 << 6)
#define APP_CLOUD_EVENTS_STATUS_UPDATED                 (0x1 << 7)

static fp_app_cloud_on_event_t _on_event;
static hsys_task_handle_t _app_cloud_task_handle;
static hsys_eventgroup_handle_t _app_cloud_event;
static bool _is_initialized = false;
// network_configs_t _network_config;
// nozzel_config_t _nozzle_config;
static fp_wake_task_t _wake;
static void * _wake_context;
hsys_queue_handle_t _nozzle_event_que;

static hsys_timer_handle_t _periodic_timer;
static hsys_timer_handle_t _hb_timer;
static void _timer_callback(void * arg);
static void _hb_timer_callback(void * arg);
const cloud_driver_t * _p_cloud_drv;

// heart_beat_info_t _hb_info;
void _app_cloud_process(void * arg);

static void _on_wifi_event(app_wifi_event_t event, void * arg);
static void _on_internet_event(app_internet_event_t event, void * arg);
static void _on_fuel_event(app_fuel_event_t event, void * arg);
static void _on_ext_disptap_event(app_disptap_event_t event, void * arg);


void app_cloud_init(const app_cloud_init_t * p_cloud_init){
// void app_cloud_init(fp_app_cloud_on_event_t fp_app_cloud_on_event, event_table_t * event_table, fp_wake_task_t fp_wake, void * wake_context){
    
    if(p_cloud_init->fp_app_cloud_on_event == NULL){
        LOG_MSG_ERROR(CLOUD_DEBUG_LOG_EN, "Critical Error!. app_cloud_init: fp_app_cloud_on_event is NULL");
        while(1);
    }
    _on_event = p_cloud_init->fp_app_cloud_on_event;

    if(p_cloud_init->app_init.event_table == NULL){
        LOG_MSG_ERROR(CLOUD_DEBUG_LOG_EN, "Critical Error!. app_wifi_init: event_table is NULL");
        while(1);
    }

    if(NULL == p_cloud_init->app_init.fp_wake || NULL == p_cloud_init->app_init.wake_context){
        LOG_MSG_ERROR(CLOUD_DEBUG_LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    _p_cloud_drv = p_cloud_init->drv;

    _wake = p_cloud_init->app_init.fp_wake;
    _wake_context = p_cloud_init->app_init.wake_context;

    _app_cloud_event = hsys_event_group_create();

    hsys_queue_init(&_nozzle_event_que, 10, sizeof(nozzle_event_t));

    p_cloud_init->app_init.event_table->on_wifi_event = (fp_event_interface_t)_on_wifi_event;
    p_cloud_init->app_init.event_table->on_internet_event = (fp_event_interface_t)_on_internet_event;
    p_cloud_init->app_init.event_table->on_fuel_event = (fp_event_interface_t)_on_fuel_event;
    p_cloud_init->app_init.event_table->on_ext_disptap_event = (fp_event_interface_t)_on_ext_disptap_event;
    
    _periodic_timer = hsys_timer_create("Periodic Timer", 60000, true, (void *)NULL, _timer_callback);
    _hb_timer = hsys_timer_create("Heartbeat Timer", 10000, false, (void *)NULL, _hb_timer_callback);

    hsys_event_group_set_bits(_app_cloud_event, APP_CLOUD_EVENTS_STARTUP);

    LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "app_cloud_init : initialized");
    _is_initialized = true;
}

static void _on_ext_disptap_event(app_disptap_event_t event, void * arg){
    switch(event)
    {
        case APP_ESP07_EVENT_FW_VERSION_LOADED:
            LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "ESP07 FW Version Loaded");
            hsys_event_group_set_bits(_app_cloud_event, APP_CLOUD_EVENTS_STATUS_UPDATED);
            WAKE_ME();
        break;

        default:
        break;
    }
}

static void _on_fuel_event(app_fuel_event_t event, void * arg){
    
    switch(event)
    {
        case APP_FUEL_EVENT_PUMPED:
            LOG_MSG_DEBUG(LOG_EN, "Fuel Pumped");
            if(hsys_queue_send(&_nozzle_event_que, arg, 0)){
                hsys_event_group_set_bits(_app_cloud_event, APP_CLOUD_EVENTS_FUEL_PUMPED);            
            }
            else{
                LOG_MSG_ERROR(CLOUD_DEBUG_LOG_EN, "Error!. _nozzle_event_que is FULL");
            }
            WAKE_ME();
        break;
        default:
        break;
    }    
}

static void _on_internet_event(app_internet_event_t event, void * arg)
{
    switch(event)
    {
        case APP_INTERNET_EVENT_CONNECTED:
            hsys_event_group_set_bits(_app_cloud_event, APP_CLOUD_EVENTS_INTERNET_CONNECTED);
            LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Internet Connected");
        break;

        case APP_INTERNET_EVENT_DISCONNECTED:
            LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Internet Disconnected");
        break;

        default:
        break;
    }

    WAKE_ME();
}

static void _on_wifi_event(app_wifi_event_t event, void * arg)
{
    static bool is_connected = true;
    switch(event)
    {
        case APP_WIFI_EVENT_STA_GOT_IP:            
            LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Wifi connected");
            hsys_event_group_set_bits(_app_cloud_event, APP_CLOUD_EVENTS_WIFI_CONNECTED);

            if(!is_connected)
            {
                hsys_event_group_set_bits(_app_cloud_event, APP_CLOUD_EVENTS_RECONNECT);
                is_connected = true;
            }

        break;

        case APP_WIFI_EVENT_STA_DISCONNECTED:
            LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Wifi disconnected");
            is_connected = false;
        break;

        default:
        break;
    }
    
    WAKE_ME();
}

typedef enum{
    cloud_wait_for_ready,
    cloud_registering,
    cloud_running
}cloud_task_t;

cloud_task_t register_to_cloud(cloud_task_t current_state)
{
    cloud_task_t new_state = cloud_wait_for_ready;

    if(_p_cloud_drv->fp_on_cloud_register_rqst!=NULL){        
        int32_t ret = _p_cloud_drv->fp_on_cloud_register_rqst(0);
        if(ret == ERROR_OK){
            LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Got device configurations %d", ret);
            if(_on_event){
                _on_event(APP_CLOUD_EVNT_NETWORK_CONFIG_READY, nullptr);
            }
            new_state = cloud_running;        
        }
        else{
            LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Failed to get device configurations, retring in 60 seconds %d", ret);            
            if(_on_event){
                _on_event(APP_CLOUD_EVNT_NETWORK_CONFIG_FAILED_RETRING, nullptr);
            }
            hsys_start_timer(_periodic_timer);
            new_state = cloud_registering;
        }
    }

    return new_state;
}

void app_cloud_run(void)
{

    // LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "app_cloud_run");

    if(!_is_initialized){
        return;
    }

    static cloud_task_t state = cloud_wait_for_ready;
    static uint32_t events;
    static uint32_t last_events = 0;
    static int32_t ret;

    LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "app_cloud_run state = %d", (uint32_t)state);

    static bool is_loop_once;

    is_loop_once = true;
    while(is_loop_once){
        is_loop_once = false;

        switch(state)
        {
            case cloud_wait_for_ready:
                last_events = APP_CLOUD_EVENTS_INTERNET_CONNECTED | APP_CLOUD_EVENTS_WIFI_CONNECTED;
                events = hsys_event_group_wait_bits
                (
                    _app_cloud_event, 
                    last_events, 
                    TRUE, TRUE, 0
                );
                
                LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Waiting for Internet and WiFi %d", (uint32_t)events);
                if(IS_EVENT(events, last_events))
                {
                    state = cloud_registering;
                    is_loop_once = true; // Loop again to process the next state
                }
            break;

            case cloud_registering:
                state = register_to_cloud(state);  
                if(state == cloud_running){
                    hsys_soft_timer_set_period(_hb_timer, 60000, 0); // 60 seconds
                    hsys_start_timer(_hb_timer);
                    LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Cloud Registered, Starting Heartbeat Timer");
                }
            break;

            case cloud_running:
                
                last_events = APP_CLOUD_EVENTS_FUEL_PUMPED | APP_CLOUD_EVENTS_PRINTED | 
                    APP_CLOUD_EVENTS_STARTUP | APP_CLOUD_EVENTS_RECONNECT | APP_CLOUD_EVENTS_HEARTBEAT |
                    APP_CLOUD_EVENTS_STATUS_UPDATED;
                events = hsys_event_group_wait_bits
                (
                    _app_cloud_event, 
                    last_events,
                    FALSE, FALSE, 0
                );

                if(IS_EVENT(events, APP_CLOUD_EVENTS_FUEL_PUMPED))
                {
                    hsys_event_group_clear_bits(_app_cloud_event, APP_CLOUD_EVENTS_FUEL_PUMPED);

                    if(hsys_queue_is_empty(&_nozzle_event_que))
                    {
                        LOG_MSG_INFO(CLOUD_DEBUG_LOG_EN, "Nozzle Event Queue is Empty");
                    }
                    else
                    {
                        nozzle_event_t ne;
                        if(hsys_queue_receive(&_nozzle_event_que, &ne, 0))
                        {
                            LOG_MSG_INFO(LOG_EN, "Nozzle Event Received");
                            ret = _p_cloud_drv->fp_on_cloud_event_pumped_rqst((void *)&ne);
                            if(ret != ERROR_OK)
                            {
                                LOG_MSG_ERROR(CLOUD_DEBUG_LOG_EN, "Error!. Failed to send fuel pumped event to cloud, ret = %d", ret);
                                _on_event(APP_CLOUD_EVNT_FUEL_PUMPED_FAILED, &ne);
                            }
                            else
                            {
                                LOG_MSG_INFO(LOG_EN, "Fuel Pumped Event Sent to Cloud");
                            }
                        }
                        else
                        {
                            LOG_MSG_ERROR(CLOUD_DEBUG_LOG_EN, "Error!. Nozzle Event Queue is Empty");
                        }
                    }

                    LOG_MSG_INFO(CLOUD_DEBUG_LOG_EN, "Fuel Pumped, Sending to Cloud");

                    if(!hsys_queue_is_empty(&_nozzle_event_que))
                    {
                        LOG_MSG_ERROR(CLOUD_DEBUG_LOG_EN, "Nozzle Event Queue is not empty, rerun cloud push");
                        hsys_event_group_set_bits(_app_cloud_event, APP_CLOUD_EVENTS_FUEL_PUMPED);
                        WAKE_ME();
                    }
                }

                if(IS_EVENT(events, APP_CLOUD_EVENTS_RECONNECT))
                {
                    hsys_event_group_clear_bits(_app_cloud_event, APP_CLOUD_EVENTS_RECONNECT);
                    _p_cloud_drv->fp_on_cloud_event_reconnect_rqst(nullptr);
                    LOG_MSG_INFO(CLOUD_DEBUG_LOG_EN, "Reconnecting to Cloud");            
                }

                if(IS_EVENT(events, APP_CLOUD_EVENTS_PRINTED))
                {
                    hsys_event_group_clear_bits(_app_cloud_event, APP_CLOUD_EVENTS_PRINTED);
                    _p_cloud_drv->fp_on_cloud_event_printed_rqst(nullptr);
                    LOG_MSG_INFO(CLOUD_DEBUG_LOG_EN, "Printed, Sending to Cloud");
                }

                if(IS_EVENT(events, APP_CLOUD_EVENTS_STARTUP))
                {
                    hsys_event_group_clear_bits(_app_cloud_event, APP_CLOUD_EVENTS_STARTUP);
                    uint32_t start_time = board_millis();
                    _p_cloud_drv->fp_on_cloud_event_startup_rqst(nullptr);

                    LOG_MSG_INFO(CLOUD_DEBUG_LOG_EN, "Startup, Sending to Cloud %ld", 
                        board_millis() - start_time);
                }
                
                if(IS_EVENT(events, APP_CLOUD_EVENTS_STATUS_UPDATED))
                {
                    hsys_event_group_clear_bits(_app_cloud_event, APP_CLOUD_EVENTS_STATUS_UPDATED);
                    uint32_t start_time = board_millis();
                    _p_cloud_drv->fp_on_cloud_event_status_updated(nullptr);
                    LOG_MSG_INFO(CLOUD_DEBUG_LOG_EN, "Status Updated, Sending to Cloud %ld", 
                        board_millis() - start_time);
                }

                if(IS_EVENT(events, APP_CLOUD_EVENTS_HEARTBEAT))
                {
                    hsys_event_group_clear_bits(_app_cloud_event, APP_CLOUD_EVENTS_HEARTBEAT);
                    uint32_t start_time = board_millis();
                    _p_cloud_drv->fp_on_cloud_event_hb_rqst(nullptr);
                    LOG_MSG_INFO(CLOUD_DEBUG_LOG_EN, "Heartbeat, Sending to Cloud %ld", 
                        board_millis() - start_time);

                    hsys_start_timer(_hb_timer);
                }

            break;
        }
    }
}

// void _app_cloud_process(void * arg){

//     hsys_event_group_wait_bits
//     (
//         _app_cloud_event, 
//         APP_CLOUD_EVENTS_INTERNET_CONNECTED | APP_CLOUD_EVENTS_WIFI_CONNECTED, 
//         pdTRUE, pdTRUE, portMAX_DELAY
//     );
    
//     LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Task started");

//     uint8_t mac_address[8];
//     board_get_mac_address(mac_address, 8);

//     char mac_address_str[SIZE_OF_MAC];
//     sprintf(mac_address_str, "%02X%02X%02X%02X%02X%02X", 
//         mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
//     LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "%s", mac_address_str);

//     int32_t ret = ERROR_OK;
//     while(1){

//         // run every one minute if failed to get the configurations
//         ret = cube_sphere_get_nozzle_config(mac_address_str, &_network_config, &_nozzle_config);
//         if(ret == ERROR_OK){
//             LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Got device configurations %d", ret);
//             if(_on_event){
//                 _on_event(APP_CLOUD_EVNT_NETWORK_CONFIG_READY, nullptr);
//             }
//             break;
//         }
//         else{
//             LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Failed to get device configurations, retring in 60 seconds %d", ret);            
//             if(_on_event){
//                 _on_event(APP_CLOUD_EVNT_NETWORK_CONFIG_FAILED_RETRING, nullptr);
//             }
//         }
        
//         hsys_task_delay(60*1000);
//     }    

//     while(1){
//         hsys_task_delay(1000);
//     }
    
//     hsys_task_delete(_app_cloud_task_handle);
//     hsys_event_group_delete(_app_cloud_event);
// }

void _timer_callback(void * arg)
{    
    if(_wake)
    {
        LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Wake From Timer");
        _wake(_wake_context);
    }
}

void _hb_timer_callback(void * arg)
{    
    if(_wake)
    {
        LOG_MSG_DEBUG(CLOUD_DEBUG_LOG_EN, "Wake From Heartbeat Timer");
        hsys_event_group_set_bits(_app_cloud_event, APP_CLOUD_EVENTS_HEARTBEAT);
        hsys_stop_timer(_hb_timer); // Stop the timer to prevent multiple calls
        _wake(_wake_context);
    }
}