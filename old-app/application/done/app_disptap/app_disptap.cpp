

#include "app_disptap.h"

#include "pal_logger.h"
#include "hsys_task.h"
#include "hsys_event.h"
#include "hsys_soft_timer.h"

#include "utils/utils.hpp"

#include "board.h"

#include "com_distap.h"
#include "cmd_distap.h"
#include "serial_flasher.h"

#include "app_config.h"
#include "pal_time.h"

#define __TAG__  "APP_DSPT"

#define DSPT_DEBUG_LOG_EN      LOG_DIS

typedef enum{
    disptap_state_wait_for_config_ready,
    disptap_waiting_for_reboot,
    disptap_wait_for_fw_update,
    disptap_running,
}disptap_state_t;

#define APP_ESP07_EVENT_CONFIG_LOADED                   (0x1 << 0)   

#define ESP07_RESET_TIMEOUT             500

static fp_app_disptap_on_event_t _on_event;
static hsys_task_handle_t _app_task_handle;
static hsys_eventgroup_handle_t _app_event;
static hsys_timer_handle_t _periodic_timer;
static bool _is_initialized = false;

static disptap_state_t disptap_state = disptap_state_wait_for_config_ready;
unsigned long ts_disptap_reset = 0;

static fp_wake_task_t _wake;
static void * _wake_context;

static void _on_config_event(app_config_event_t event, void * arg);
static void _fuel_event_display_01(display_type_t type, uint8_t *data);
static void _fuel_event_display_02(display_type_t type, uint8_t *data);
static void _timer_callback(void * arg);

void app_disptap_init(const app_disptap_init_t * p_disptap_init){

    if(p_disptap_init->fp_app_disptap_on_event == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_cloud_init: fp_app_cloud_on_event is NULL");
        while(1);
    }
    _on_event = p_disptap_init->fp_app_disptap_on_event;

    if(p_disptap_init->app_init.event_table == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_wifi_init: event_table is NULL");
        while(1);
    }

    if(NULL == p_disptap_init->app_init.fp_wake || NULL == p_disptap_init->app_init.wake_context){
        LOG_MSG_ERROR(LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    _wake = p_disptap_init->app_init.fp_wake;
    _wake_context = p_disptap_init->app_init.wake_context;  

    _app_event = hsys_event_group_create();
    
    p_disptap_init->app_init.event_table->on_config_event = (fp_event_interface_t)_on_config_event;
    
    _periodic_timer = hsys_timer_create("Periodic Timer", 15000, true, (void *)NULL, _timer_callback);

    LOG_MSG_DEBUG(LOG_EN, "app_disptap_init : initialized");
    _is_initialized = true;
}

static void _on_config_event(app_config_event_t event, void * arg){

    if(APP_CONFIG_EVENT_LOADED == event){
        hsys_event_group_set_bits(_app_event, APP_ESP07_EVENT_CONFIG_LOADED);
        
        if(_wake){
            LOG_MSG_DEBUG(LOG_EN, "Wake From Config");
            _wake(_wake_context);
        }
    }    
}

void app_disptap_run(void){

    // LOG_MSG_DEBUG(LOG_EN, "app_disptap_run %d", (int)disptap_state);

    if(!_is_initialized){
        return;
    }

    static uint32_t wait_events;
    static uint32_t events;
    switch(disptap_state){

        case disptap_state_wait_for_config_ready:
        {
            wait_events = APP_ESP07_EVENT_CONFIG_LOADED;
            events = hsys_event_group_wait_bits(_app_event, wait_events, FALSE, FALSE, 0);
            if(IS_EVENT(events, APP_ESP07_EVENT_CONFIG_LOADED)){
                
                hsys_event_group_clear_bits(_app_event, APP_ESP07_EVENT_CONFIG_LOADED);
                
                display_type_t dt;
                int32_t ret = app_config_get_display_type((uint32_t *)(&dt), 3000);
                
                if(ret != ESP_OK){
                    LOG_MSG_ERROR(LOG_EN, "Failed to get display type, ret = %d", ret);
                }
                
                init_comms_distap(_fuel_event_display_01, _fuel_event_display_02);    
                gpio_set_reset_distap(false);
                
                disptap_state = disptap_waiting_for_reboot;
                ts_disptap_reset = pal_time_get_ms();
                
                hsys_start_timer(_periodic_timer);
            }
        }
        break;

        case disptap_waiting_for_reboot:
        {
            if((pal_time_get_ms() - ts_disptap_reset) > ESP07_RESET_TIMEOUT){
                display_type_t dt;
                int32_t ret = app_config_get_display_type((uint32_t *)(&dt), 3000);
                if(ret != ESP_OK){
                    LOG_MSG_ERROR(LOG_EN, "Failed to get display type, ret = %d", ret);
                }
                distap_set_display_type(dt);
                start_serial_flash(false);
            
                char disptap_version[SIZE_OF_DISPTAP_FW_VERSION] = {0};
                distap_get_fw_version((char *)(disptap_version));

                LOG_MSG_DEBUG(LOG_EN, "DISPTAP Firmware Version: %s", disptap_version);
                _on_event(APP_ESP07_EVENT_FW_VERSION_LOADED, (void *)disptap_version);
                // publish esp07 firmware version

                distap_get_fw_name();
                distap_get_fw_timedate();
                
                distap_set_display_type(dt);

                disptap_state = disptap_running;

                hsys_stop_timer(_periodic_timer);
                LOG_MSG_DEBUG(LOG_EN, "DISPTAP is running");
            }
        }
        break;

        case disptap_wait_for_fw_update:
        break;

        case disptap_running:
        break;

        default:
        break;
    }

    // LOG_MSG_DEBUG(LOG_EN, "app_disptap_run : Completed");
}

static void _fuel_event_display_01(display_type_t type, uint8_t *data){
    
    static app_disptap_display_data_t display_1_data = {0};
    display_1_data.type = type;

    const display_data_t *dis = (const display_data_t *)data;
    display_1_data.data.flags.start_stop = dis->flags.bits.start_stop;
    display_1_data.data.errors = dis->error.u8int;
    display_1_data.data.unit_price = dis->unit_price;
    display_1_data.data.total_price = dis->total_price;
    display_1_data.data.volume_l = dis->volume_l;   
    
    if(NULL != _on_event){
        // LOG_MSG_DEBUG(DSPT_DEBUG_LOG_EN, "Display 1 Data Ready");
        _on_event(APP_DISPTAP_EVENT_DISPLAY1_DATA_READY, (void *)&display_1_data);
    }
}

static void _fuel_event_display_02(display_type_t type, uint8_t *data){

    static app_disptap_display_data_t display_2_data = {0};
    display_2_data.type = type;

    const display_data_t *dis = (const display_data_t *)data;
    display_2_data.data.flags.start_stop = dis->flags.bits.start_stop;
    display_2_data.data.errors = dis->error.u8int;
    display_2_data.data.unit_price = dis->unit_price;
    display_2_data.data.total_price = dis->total_price;
    display_2_data.data.volume_l = dis->volume_l;    
    
    if(NULL != _on_event){
        // LOG_MSG_DEBUG(DSPT_DEBUG_LOG_EN, "Display 2 Data Ready");
        _on_event(APP_DISPTAP_EVENT_DISPLAY2_DATA_READY, (void *)&display_2_data);
    }
}

void _timer_callback(void * arg)
{    
    if(_wake){
        // LOG_MSG_DEBUG(LOG_EN, "Wake From Timer");
        _wake(_wake_context);
    }
}
