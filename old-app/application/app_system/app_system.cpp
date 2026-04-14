

#include "app.h"

#include "pal_logger.h"
#include "hsys_task.h"
#include "hsys_event.h"
#include "hsys_soft_timer.h"
#include "board.h"

#define __TAG__  "APP_SYS "

#define USER_REBOOT_TIMER_SEC                   (30)

#define APP_SYSTEM_EVENT_ON_WIFI_NO_SIGNAL      (0x1<<0)
#define APP_SYSTEM_EVENT_REBOOT_TIMER_EXPIRED   (0x1<<0)

static hsys_task_handle_t _app_task_handle;
static hsys_eventgroup_handle_t _app_event;
static hsys_timer_handle_t _reboot_timer;

static void _app_process(void * arg);
static void _on_config_event(app_config_event_t event, void * arg);
static void _on_wifi_event(app_wifi_event_t event, void * arg);
void _app_timer_cb(void * arg);


static fp_app_sys_on_event_t _on_event;

static bool _is_initialized = false;

void _app_process();

void app_sys_init(fp_app_sys_on_event_t fp_app_wifi_on_event, event_table_t * event_table, int32_t priority){

    if(fp_app_wifi_on_event == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_wifi_init: fp_app_wifi_on_event is NULL");
        while(1);
    }
    _on_event = fp_app_wifi_on_event;

    if(event_table == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_wifi_init: event_table is NULL");
        while(1);
    }

    _reboot_timer = hsys_timer_create("Reboot Timer", USER_REBOOT_TIMER_SEC, false, (void *)NULL, _app_timer_cb);
    if(_reboot_timer == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_wifi_init: hsys_timer_create failed");
        while (1);
    }    

    _app_event = hsys_event_group_create();
    if(_app_event == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_wifi_init: hsys_event_group_create failed");
        while (1);
    }

    _app_task_handle = hsys_task_create(
        _app_process,  // Task function
        "System Task",         // Task name
        4*1024,                // Stack size
        NULL,                  // Parameters
        priority                      // Priority
    );

    event_table->on_config_event = _on_config_event;
    event_table->on_wifi_event = _on_wifi_event;

    LOG_MSG_DEBUG(LOG_EN, "app_wifi_init : initialized");

    _is_initialized = true;
}

static void _on_config_event(app_config_event_t event, void * arg){

    switch(event){        
        case APP_CONFIG_EVENT_LOADED:            
            break;
        default:
            break;
    }
}

static void _on_wifi_event(app_wifi_event_t event, void * arg){
    switch(event){
        case APP_WIFI_EVENT_NO_AVAILABLE_SIGNAL:
            hsys_event_group_set_bits(_app_event, APP_SYSTEM_EVENT_ON_WIFI_NO_SIGNAL);
            break;
        default:
            break;
    }
}

void _app_process(void * arg){

    while(1){

        const uint32_t events = APP_SYSTEM_EVENT_ON_WIFI_NO_SIGNAL | APP_SYSTEM_EVENT_REBOOT_TIMER_EXPIRED;
        static uint32_t events_flagged;
        events_flagged = hsys_event_group_wait_bits(_app_event, events, TRUE, FALSE, 0);
        
        if((events_flagged & APP_SYSTEM_EVENT_ON_WIFI_NO_SIGNAL) == events){
            if(_on_event){
                uint32_t seconds_left = 30;
                _on_event(APP_SYS_EVENT_REBOOT_SCHEDULED_IN_X_SECONDS, (void *)seconds_left);
                hsys_start_timer(_reboot_timer);
            }
        }

        if((events_flagged & APP_SYSTEM_EVENT_REBOOT_TIMER_EXPIRED) == events){
            board_restart();
        }
    }

    hsys_task_delete(_app_task_handle);
    hsys_event_group_delete(_app_event);
}

void _app_timer_cb(void * arg){
    hsys_event_group_set_bits(_app_event, APP_SYSTEM_EVENT_REBOOT_TIMER_EXPIRED);    
}