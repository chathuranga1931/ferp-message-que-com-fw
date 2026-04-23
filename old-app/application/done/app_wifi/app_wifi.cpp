

#include "app.h"
#include "app_wifi.h"

#include "pal_wifi.h"
#include "pal_logger.h"
#include "hsys_task.h"
#include "hsys_event.h"
#include "hsys_mutex.h"
#include "hsys_soft_timer.h"

#include "board.h"

#include <string.h>
#include <ctime>

#define __TAG__  "APP_WIFI"

#define WIFI_DEBUG_LOG_EN      LOG_DIS
#define WIFI_WARN_LOG_EN       LOG_DIS
#define WIFI_ERROR_LOG_EN      LOG_DIS
#define WIFI_INFO_LOG_EN       LOG_DIS

#define APP_WIFI_EVENTS_READY_TO_START            (0x1 << 0)

static fp_app_wifi_on_event_t _on_event;
app_wifi_init_t const * _wifi_init;

static hsys_mutex_handle_t _hsys_wifi_mutex_handle;
static hsys_timer_handle_t _periodic_timer;
static pal_wifi_status_t _wifi_status;
static hsys_eventgroup_handle_t _app_wifi_event;
static bool _is_initialized = false;
static bool _wifi_pal_initialized = false;

uint8_t _retry_count;

static fp_wake_task_t _wake;
static void * _wake_context;

// static void _app_wifi_process(void * arg);
static void _on_config_event(app_config_event_t event, void * arg);
static void _timer_callback(void * arg);

#define LOCK_HSYS_WIFI() hsys_mutex_lock(_hsys_wifi_mutex_handle)
#define UNLOCK_HSYS_WIFI() hsys_mutex_unlock(_hsys_wifi_mutex_handle)

// void app_wifi_init(fp_app_wifi_on_event_t fp_app_wifi_on_event, event_table_t * event_table, fp_wake_task_t fp_wake, void * wake_context){
void app_wifi_init(const app_wifi_init_t * p_wifi_init){

    if(p_wifi_init->fp_app_wifi_on_event == NULL){
        LOG_MSG_ERROR(WIFI_DEBUG_LOG_EN, "Critical Error!. app_wifi_init: fp_app_wifi_on_event is NULL");
        while(1);
    }
    _on_event = p_wifi_init->fp_app_wifi_on_event;

    if(p_wifi_init->app_init.event_table == NULL){
        LOG_MSG_ERROR(WIFI_DEBUG_LOG_EN, "Critical Error!. app_wifi_init: event_table is NULL");
        while(1);
    }

    _hsys_wifi_mutex_handle = hsys_mutex_create();
    if(_hsys_wifi_mutex_handle == NULL){
        LOG_MSG_ERROR(WIFI_DEBUG_LOG_EN, "Critical Error!. app_wifi_init: hsys_mutex_create failed");
        while (1);
    }

    if(NULL == p_wifi_init->app_init.fp_wake || NULL == p_wifi_init->app_init.wake_context){
        LOG_MSG_ERROR(WIFI_DEBUG_LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    _wifi_init = p_wifi_init;
    _wake = p_wifi_init->app_init.fp_wake;
    _wake_context = p_wifi_init->app_init.wake_context;

    p_wifi_init->app_init.event_table->on_config_event = (fp_event_interface_t)_on_config_event;    
    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "%ld", p_wifi_init->app_init.event_table);
    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "%ld", p_wifi_init->app_init.event_table->on_config_event);
    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "%ld", _on_config_event);

    _app_wifi_event = hsys_event_group_create();
    if(_app_wifi_event == NULL)
    {
        LOG_MSG_ERROR(WIFI_DEBUG_LOG_EN, "Critical Error!. app_wifi_init: hsys_event_group_create failed");
        while (1);
    }
    
    _periodic_timer = hsys_timer_create("Periodic Timer", 15000, true, (void *)NULL, _timer_callback);
    hsys_start_timer(_periodic_timer);

    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "app_wifi_init : initialized");

    _is_initialized = true;
}

static void _on_config_event(app_config_event_t event, void * arg){

    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "on config event %d", (int)event);

    switch(event){        
        case APP_CONFIG_EVENT_LOADED:            
            if(_app_wifi_event){
                hsys_event_group_set_bits(_app_wifi_event, APP_WIFI_EVENTS_READY_TO_START);
            }
            break;
        default:
            break;
    }
    
    if(_wake){
        LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Wake From Config");
        _wake(_wake_context);
    }
}

void _on_pal_wifi_event(pal_wifi_event_t event, void* event_data, void* user_data)
{
    static pal_wifi_event_t event_prev = (pal_wifi_event_t)-1;
    
    // Map PAL WiFi events to app WiFi events
    app_wifi_event_t app_event;
    bool trigger_callback = false;
    
    switch(event)
    {
        case PAL_WIFI_EVENT_STA_GOT_IP:
            app_event = (app_wifi_event_t)PAL_WIFI_EVENT_STA_GOT_IP;
            trigger_callback = true;
            _wifi_status.is_connected = true;
            _retry_count = 0;
            LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "WiFi connected");
            break;

        case PAL_WIFI_EVENT_STA_DISCONNECTED:
            app_event = (app_wifi_event_t)PAL_WIFI_EVENT_STA_DISCONNECTED;
            trigger_callback = true;
            _wifi_status.is_connected = false;
            LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "WiFi disconnected");
            break;
            
        case PAL_WIFI_EVENT_STA_CONNECTED:
            // Don't trigger app callback yet, wait for IP
            trigger_callback = false;
            break;
            
        case PAL_WIFI_EVENT_AP_START:
            app_event = (app_wifi_event_t)PAL_WIFI_EVENT_AP_START;
            trigger_callback = true;
            break;
            
        case PAL_WIFI_EVENT_AP_STACONNECTED:
            app_event = (app_wifi_event_t)PAL_WIFI_EVENT_AP_STACONNECTED;
            trigger_callback = true;
            break;
            
        case PAL_WIFI_EVENT_AP_STADISCONNECTED:
            app_event = (app_wifi_event_t)PAL_WIFI_EVENT_AP_STADISCONNECTED;
            trigger_callback = true;
            break;

        default:
            trigger_callback = false;
            break;
    }
    
    if(trigger_callback && event_prev != event)
    {
        event_prev = event;
        if(_on_event)
        {
            _on_event(app_event, &_wifi_status);
        }

        if(_wake){
            LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Wake From PAL WiFi");
            _wake(_wake_context);
        }
    }
}

typedef enum 
{
    wifi_app_waiting_for_configs_to_load,
    wifi_app_load_config,
    wifi_app_init,
    wifi_app_connect, 
    wifi_app_reconnect,
    wifi_app_connecting,  
    wifi_app_monitoring,
}wifi_app_state_t;

void app_wifi_run(){

    if(!_is_initialized){
        return;
    }

    static wifi_app_state_t wifi_app_state = wifi_app_waiting_for_configs_to_load;
    static uint32_t event;
    static uint32_t ts_connect_rqst = board_millis();       
    static pal_wifi_init_config_t pal_wifi_config;
    int32_t ret;
    
    bool need_one_iteration = true;
    while(need_one_iteration){
        need_one_iteration = false;

        LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Processing...");

        switch(wifi_app_state)
        {
            case wifi_app_waiting_for_configs_to_load:
            {
                // LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "wifi_app_waiting_for_configs_to_load");
                event = hsys_event_group_wait_bits(_app_wifi_event, APP_WIFI_EVENTS_READY_TO_START, 1, 0, 0);
                if(APP_WIFI_EVENTS_READY_TO_START == event)
                {
                    wifi_app_state  = wifi_app_load_config;
                    need_one_iteration = true;
                }
            }
            break;

            case wifi_app_load_config:
            {
                LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Loading configuration for WIFI");         
                ret = _wifi_init->fp_app_wifi_get_init_config(&pal_wifi_config, 2000);
                if(ret == ERROR_OK){
                    wifi_app_state = wifi_app_init;
                    need_one_iteration = true;
                }
                else{
                    LOG_MSG_ERROR(WIFI_DEBUG_LOG_EN, "Critical Error!. get wifi_init failed");           
                }
            }     
            break;

            case wifi_app_init:
            {            
                LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "PAL WiFi Initializing");
                ret = pal_wifi_init(&pal_wifi_config, _on_pal_wifi_event, NULL);    
                if(ret == 0)
                {
                    _wifi_pal_initialized = true;
                    
                    // Start WiFi
                    ret = pal_wifi_start();
                    if(ret == 0) {
                        wifi_app_state = wifi_app_connect;
                        need_one_iteration = true;
                    } else {
                        LOG_MSG_ERROR(WIFI_DEBUG_LOG_EN, "Failed to start WiFi");
                    }
                }
                else {
                    LOG_MSG_ERROR(WIFI_DEBUG_LOG_EN, "Failed to initialize WiFi");
                }
            }
            break;

            case wifi_app_connect:
            {
                if(pal_wifi_config.mode == PAL_WIFI_MODE_STA) {
                    pal_wifi_sta_connect();
                    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Connecting to WiFi");
                } else {
                    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "AP mode started");
                }
                ts_connect_rqst = board_millis();
                wifi_app_state = wifi_app_connecting;
            } 
            break;
            
            case wifi_app_reconnect:
            {
                pal_wifi_sta_disconnect();
                hsys_task_delay(100);
                pal_wifi_sta_connect();
                ts_connect_rqst = board_millis();
                wifi_app_state = wifi_app_connecting;
                _retry_count++;
                
                if(_retry_count > USER_MAX_NO_WIFI_CONNECT_RETRY_COUNT)
                {
                    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "No Available Signal, Retry count exceeded");
                    _retry_count = 0;
                    _on_event(APP_WIFI_EVENT_NO_AVAILABLE_SIGNAL, NULL);
                }
            }
            break;

            case wifi_app_connecting:
            {
                if(_wifi_status.is_connected)
                {
                    wifi_app_state = wifi_app_monitoring;
                    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Connected to WiFi, Monitoring");
                }
                else if(board_millis() - ts_connect_rqst >= 60000)
                {
                    wifi_app_state = wifi_app_reconnect;
                    LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Connecting to WiFi, Timed out, reconnecting");
                    need_one_iteration = true;
                }
            }
            break;

            case wifi_app_monitoring:
            {
                if(pal_wifi_config.mode == PAL_WIFI_MODE_STA) {
                    // Update connection status
                    _wifi_status.is_connected = pal_wifi_sta_is_connected();
                    
                    if(!_wifi_status.is_connected)
                    {
                        LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Reconnecting to WiFi");   
                        wifi_app_state = wifi_app_reconnect;
                        need_one_iteration = true;
                    }
                    else {
                        // Update RSSI
                        int8_t rssi;
                        if(pal_wifi_sta_get_rssi(&rssi) == 0) {
                            // Apply smoothing filter
                            _wifi_status.rssi = (int8_t)(_wifi_status.rssi * 0.5 + rssi * 0.5);
                        }

                        static uint32_t ts_rssi_event = 0;
                        if((board_millis() - ts_rssi_event) >= 30000)
                        {
                            ts_rssi_event = board_millis();                    
                            _on_event((app_wifi_event_t)PAL_WIFI_EVENT_STA_GOT_IP, (void *)(&_wifi_status.rssi));
                            LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "RSSI Changed: %d", _wifi_status.rssi);
                        }
                    }
                }
            }
            break;

            default:
            break;
        }
    }
}

void _timer_callback(void * arg)
{    
    if(_wake){
        // LOG_MSG_DEBUG(WIFI_DEBUG_LOG_EN, "Wake From Timer");
        _wake(_wake_context);
    }
}