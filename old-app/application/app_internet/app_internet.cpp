
#include <stdint.h>
#include <stdbool.h>

#include "app_internet.h"
#include "app_wifi/app_wifi.h"

#include "pal_network.h"  // PAL network interface

#include "pal_logger.h"

#include "hsys_task.h"
#include "hsys_event.h"
#include "hsys_queue.h"
#include "hsys_mutex.h"
#include "hsys_soft_timer.h"

#include "board.h"

#include "app.h"

#define __TAG__  "APP_INT "

#define INT_DEBUG_LOG_EN      LOG_DIS
#define INT_WARN_LOG_EN       LOG_DIS
#define INT_ERROR_LOG_EN      LOG_DIS
#define INT_INFO_LOG_EN       LOG_DIS

#define INTMON_EVENT_WIFI_CONNECTED               (0x1 << 0)
#define INTMON_EVENT_WIFI_DISCONNECTED            (0x1 << 1)

// IP to Ping (Google's DNS server)
const char* host = "8.8.8.8";

// static device_t * _device;
static bool _is_initialized = false;
static hsys_eventgroup_handle_t _app_int_events;
static hsys_timer_handle_t _periodic_timer;

static bool _is_wifi_connected = false;

static fp_wake_task_t _wake;
static void * _wake_context;

static void _timer_callback(void * arg);
static void _on_wifi_event(app_wifi_event_t event, void * arg);

static fp_app_internet_on_event_t _on_event;
static bool _is_ping_timeout = false;

static void app_processing();

static void _on_wifi_event(app_wifi_event_t event, void * arg){

    switch(event)
    {
        case APP_WIFI_EVENT_STA_GOT_IP:
            _is_wifi_connected = true;
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Internet Monitor WiFI Ready");
            hsys_event_group_clear_bits(_app_int_events, INTMON_EVENT_WIFI_DISCONNECTED);
            hsys_event_group_set_bits(_app_int_events, INTMON_EVENT_WIFI_CONNECTED);
        break;

        case APP_WIFI_EVENT_STA_DISCONNECTED:
            _is_wifi_connected = false;
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Internet Monitor WiFI disconnected");
            hsys_event_group_clear_bits(_app_int_events, INTMON_EVENT_WIFI_CONNECTED);
            hsys_event_group_set_bits(_app_int_events, INTMON_EVENT_WIFI_DISCONNECTED);
        break;

        case APP_WIFI_EVENT_STA_RSSI_CHANGED:
        break;

        default:
        break;
    }

    if(_wake)
    {
        LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Wake from WiFi Event");
        _wake(_wake_context);
    }
}

void app_internet_init(const app_internet_init_t * p_internet_init){
    
    if(NULL == p_internet_init->fp_app_internet_on_event){
        LOG_MSG_ERROR(INT_DEBUG_LOG_EN, "Critical Error!. Internet Monitor Null pointer reference, please check.., Critical Error");
        while (1);
    }
    _on_event = p_internet_init->fp_app_internet_on_event;

    if(p_internet_init->app_init.event_table == NULL){
        LOG_MSG_ERROR(INT_DEBUG_LOG_EN, "WIFI Null pointer reference, please check.., Critical Error");
        while (1);        
    }

    _app_int_events = hsys_event_group_create();

    if(NULL == p_internet_init->app_init.fp_wake || NULL == p_internet_init->app_init.wake_context){
        LOG_MSG_ERROR(INT_DEBUG_LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    _wake = p_internet_init->app_init.fp_wake;
    _wake_context = p_internet_init->app_init.wake_context;

    _periodic_timer = hsys_timer_create("Periodic Timer", 60000, true, (void *)NULL, _timer_callback);     
    
    p_internet_init->app_init.event_table->on_wifi_event = (fp_event_interface_t)_on_wifi_event;  
    
    _is_initialized = true;
    LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Internet Monitor initialized.");
}

enum app_state_t
{
    APP_WAIT_FOR_READY,
    APP_PROCESSING,
};

app_state_t state = APP_PROCESSING;

static void app_processing()
{
    static uint32_t internet_status = -1;
    if(_is_wifi_connected)
    {        
        // First check if we have an IP address
        char ip_buffer[16] = {0};
        if (pal_network_get_ip_address(ip_buffer, sizeof(ip_buffer)) == 0) {
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Current IP: %s", ip_buffer);
        } else {
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "No IP address yet, skipping ping");
            return;
        }
        
        // Use PAL network ping function
        LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Pinging %s...", host);
        bool is_pinged = pal_network_ping(host, 2000);  // 2 second timeout
        
        if(is_pinged)
        {
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Ping Success");
        }
        else 
        {
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Ping Failed");
        }

        if(is_pinged & (internet_status != APP_INTERNET_EVENT_CONNECTED))
        {
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Internet Connected");
            if(_on_event)
            {
                _on_event(APP_INTERNET_EVENT_CONNECTED, NULL);    
            }            
            internet_status = APP_INTERNET_EVENT_CONNECTED;
        }
        else if(!is_pinged & (internet_status != APP_INTERNET_EVENT_DISCONNECTED))
        {
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Internet Disconnected");
            if(_on_event)
            {
                _on_event(APP_INTERNET_EVENT_DISCONNECTED, NULL);
            }
            internet_status = APP_INTERNET_EVENT_DISCONNECTED;
        }
    }
}

void app_internet_run()
{
    if(!_is_initialized)
    {
        return;
    }   
    
    static uint32_t events;

    switch(state)
    {
        case APP_WAIT_FOR_READY:
        {
            LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Wait For Ready");

            const uint32_t wait_events = (INTMON_EVENT_WIFI_CONNECTED);
            events = hsys_event_group_wait_bits(_app_int_events, wait_events, TRUE, FALSE, 0);            
            if((events & wait_events) == wait_events) 
            {
                LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "State : Ready --> Monitoring");        
                app_processing();

                // Once WiFi is connected, need to start the periodic timer to check for internet connectivity
                hsys_start_timer(_periodic_timer);
                state = APP_PROCESSING;
            }
        }
        break;
        
        case APP_PROCESSING:
        {
            const uint32_t wait_events = (INTMON_EVENT_WIFI_DISCONNECTED);
            events = hsys_event_group_wait_bits(_app_int_events, wait_events, TRUE, FALSE, 0);
            if((events & wait_events) == wait_events) 
            {
                LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "WiFi Disconnected, stop internet monitoring");
                hsys_stop_timer(_periodic_timer);

                state = APP_WAIT_FOR_READY;
            }
            else
            {
                LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Processing...");
                app_processing();
            }
        }
        break;
    }
}

void _timer_callback(void * arg)
{
    if(_wake)
    {
        LOG_MSG_DEBUG(INT_DEBUG_LOG_EN, "Wake From Timer");
        _wake(_wake_context);
    }
}
