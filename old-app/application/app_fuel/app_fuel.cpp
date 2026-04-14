

#include "app.h"

#include "pal_logger.h"
#include "hsys_task.h"
#include "hsys_event.h"
#include "hsys_queue.h"

#include "pumps/nozzle_event.h"
#include "pumps/sanki/sanki_6_digit_1.h"

#include "utils/utils.hpp"

#include "hsys_tog_button.h"

#include "pal_time.h"

#include "board.h"

#include "display_types.h"

#include "app_fuel.h"
#include "app_disptap.h"

#define __TAG__  "APP_FUEL"

#define FUEL_DEBUG_EN      LOG_DIS
#define FUEL_WARN_EN       LOG_EN
#define FUEL_ERROR_EN      LOG_EN
#define FUEL_INFO_EN       LOG_EN

#define APP_FUEL_EVENT_NOZZLE1_START                        (0x1 << 0) 
#define APP_FUEL_EVENT_NOZZLE1_STOP                         (0x1 << 1) 
#define APP_FUEL_EVENT_NOZZLE2_START                        (0x1 << 2) 
#define APP_FUEL_EVENT_NOZZLE2_STOP                         (0x1 << 3)

static fp_app_fuel_on_event_t _on_event;
static hsys_task_handle_t _app_task_handle;
static hsys_eventgroup_handle_t _app_event;
static hsys_queue_handle_t _app_display_que[NO_NOZZELS];
static hsys_tog_button_t nozzle_1;
static hsys_tog_button_t nozzle_2;

static bool _is_initialized = false;

static fp_wake_task_t _wake;
static void * _wake_context;

static void _app_process_disp1(void * arg);
static void _app_process_disp2(void * arg);
static void _on_ext_disptap_event(app_disptap_event_t event, void * arg);

static void _app_process_display_data(uint8_t n_idx, bool nozzle_state);

void on_nozzle1_start();
void on_nozzle1_stop();
void on_nozzle2_start();
void on_nozzle2_stop();

void app_fuel_init(const app_fuel_init_t * p_webserver_init){
    
    if(p_webserver_init->fp_app_fuel_on_event == NULL){
        LOG_MSG_ERROR(FUEL_DEBUG_EN, "Critical Error!. app_cloud_init: fp_app_cloud_on_event is NULL");
        while(1);
    }
    _on_event = p_webserver_init->fp_app_fuel_on_event;

    if(p_webserver_init->app_init.event_table == NULL){
        LOG_MSG_ERROR(FUEL_DEBUG_EN, "Critical Error!. app_wifi_init: event_table is NULL");
        while(1);
    }

    for(int i=0; i<NO_NOZZELS; i++){
        bool is_queue_initialized = hsys_queue_init(&_app_display_que[i], 10, sizeof(app_disptap_display_data_t));
        if(!is_queue_initialized){
            LOG_MSG_ERROR(FUEL_DEBUG_EN, "Critical Error!. app_esp07_init: _app_display_que is NULL");
            while(1);
        }
    }

    _app_event = hsys_event_group_create();

    hsys_tog_button_init(
        &nozzle_1,
        on_nozzle1_stop,
        on_nozzle1_start,
        500,
        500
    );

    hsys_tog_button_init(
        &nozzle_2,
        on_nozzle2_stop,
        on_nozzle2_start,
        500,
        500
    );
    
    if(NULL == p_webserver_init->app_init.fp_wake || NULL == p_webserver_init->app_init.wake_context){
        LOG_MSG_ERROR(FUEL_DEBUG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    _wake = p_webserver_init->app_init.fp_wake;
    _wake_context = p_webserver_init->app_init.wake_context;  
   
    p_webserver_init->app_init.event_table->on_ext_disptap_event = (fp_event_interface_t)_on_ext_disptap_event;
    
    board_register_cb_on_button_nozzle1_start(hsys_tog_button_press_event, (void *)&nozzle_1);
    board_register_cb_on_button_nozzle1_stop(hsys_tog_button_release_event, (void *)&nozzle_1);
    board_register_cb_on_button_nozzle2_start(hsys_tog_button_press_event, (void *)&nozzle_2);
    board_register_cb_on_button_nozzle2_stop(hsys_tog_button_release_event, (void *)&nozzle_2);

    LOG_MSG_DEBUG(FUEL_DEBUG_EN, "app_fuel_init : initialized");
    _is_initialized = true;
}
 
void app_fuel_run(){  

    // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "app_fuel_run");
    
    if(!_is_initialized){
        return;
    }

    static bool nozzle_state[NO_NOZZELS] = {0}; 
    static uint32_t events_triggerd = 0;
    events_triggerd = hsys_event_group_wait_bits(_app_event, 
        APP_FUEL_EVENT_NOZZLE1_START | APP_FUEL_EVENT_NOZZLE1_STOP | APP_FUEL_EVENT_NOZZLE2_START | APP_FUEL_EVENT_NOZZLE2_STOP,
        FALSE, FALSE, 0);
    
    if(IS_EVENT(events_triggerd, APP_FUEL_EVENT_NOZZLE1_START)){
        hsys_event_group_clear_bits(_app_event, APP_FUEL_EVENT_NOZZLE1_START);
        nozzle_state[0] = true;
        LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Nozzle 1 started");
    }

    if(IS_EVENT(events_triggerd, APP_FUEL_EVENT_NOZZLE1_STOP)){
        hsys_event_group_clear_bits(_app_event, APP_FUEL_EVENT_NOZZLE1_STOP);
        nozzle_state[0] = false;
        LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Nozzle 1 stopped");
    }

    if(IS_EVENT(events_triggerd, APP_FUEL_EVENT_NOZZLE2_START)){
        hsys_event_group_clear_bits(_app_event, APP_FUEL_EVENT_NOZZLE2_START);
        nozzle_state[1] = true;
        LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Nozzle 2 started");
    }
    
    if(IS_EVENT(events_triggerd, APP_FUEL_EVENT_NOZZLE2_STOP)){
        hsys_event_group_clear_bits(_app_event, APP_FUEL_EVENT_NOZZLE2_STOP);
        nozzle_state[1] = false;
        LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Nozzle 2 stopped");
    }

    for(int n_idx=0; n_idx<NO_NOZZELS; n_idx++){
        _app_process_display_data(n_idx, nozzle_state[n_idx]);
    }

    // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "app_fuel_run : completed");
}

static void _on_ext_disptap_event(app_disptap_event_t event, void * arg){
    switch(event){
        case APP_DISPTAP_EVENT_DISPLAY1_DATA_READY:
            if(NULL != arg){
                bool is_sent = hsys_queue_send(&_app_display_que[0], arg, 10);
                if(!is_sent)
                {
                    LOG_MSG_ERROR(FUEL_DEBUG_EN, "Error!. _app_display1_que is FULL");
                }
                else
                {
                    app_disptap_display_data_t * data = (app_disptap_display_data_t *)arg;
                    LOG_MSG_DEBUG(FUEL_DEBUG_EN, "D1: T=%d, U=%d, P=%d, V=%d", 
                        data->type, data->data.unit_price, data->data.total_price, data->data.volume_l);
                }
            }
        break;
        case APP_DISPTAP_EVENT_DISPLAY2_DATA_READY:
            if(NULL != arg){
                bool is_sent = hsys_queue_send(&_app_display_que[1], arg, 10);
                if(!is_sent)
                {
                    LOG_MSG_ERROR(FUEL_DEBUG_EN, "Error!. _app_display1_que is FULL");
                }
                else
                {
                    app_disptap_display_data_t * data = (app_disptap_display_data_t *)arg;
                    LOG_MSG_DEBUG(FUEL_DEBUG_EN, "D2: T=%d, U=%d, P=%d, V=%d", 
                        data->type, data->data.unit_price, data->data.total_price, data->data.volume_l);
                }
            }
        break;
        default:
        break;
    } 
    
    if(_wake){
        // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Wake From ESP07 Event");
        _wake(_wake_context);
    }
}

static void _app_process_display_data(uint8_t n_idx, bool nozzle_state){

    static app_disptap_display_data_t _app_display_data[NO_NOZZELS] = {0};
    static app_display_data_t display_data[NO_NOZZELS] = {0}; 
    static unsigned long ts_last_data_received[NO_NOZZELS] =  {0};
    bool is_pumped[NO_NOZZELS] = {0};

    bool is_received = hsys_queue_receive(&_app_display_que[n_idx], &_app_display_data[n_idx], 0);
    if(is_received) { ts_last_data_received[n_idx] = pal_time_get_ms(); } 

    // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Processing Display Data for Nozzle %d, Received=%d, Nozzle State=%d", n_idx+1, is_received, nozzle_state);
    
    switch (_app_display_data[n_idx].type){

        case DIS_LONGFENG_8_DIGIT:
            // fuel_event_process_hongyang_8_digit(n_idx, data);
            break;

        case DIS_CENSTAR_7_DIGIT:
            // fuel_event_process_censtar_7_digit(n_idx, data);
            break;

        case DIS_WAYNE_6_DIGIT:
            // fuel_event_process_wayne_6_digit(n_idx, data);
            break;

        case DIS_CENSTAR_6_DIGIT:
            // fuel_event_process_censtar_6_digit(n_idx, data);
            break;

        case DIS_SANKI_6_DIGIT:
        {
            if(is_received)
            {
                // LOG_MSG_DEBUG_STR(
                //     "Sanki 6 "+ String(n_idx+1) + " = " + 
                //     String(_app_display_data[n_idx].data.unit_price / 100.0, 2) + ", " + 
                //     String(_app_display_data[n_idx].data.total_price / 100.0, 2) + ", " + 
                //     String(_app_display_data[n_idx].data.volume_l / 1000.0, 3)
                // );
            
                display_data[n_idx].total_pricex100 = (uint64_t)_app_display_data[n_idx].data.total_price;
                display_data[n_idx].volume_lx1000 = (uint64_t)_app_display_data[n_idx].data.volume_l;
                display_data[n_idx].unit_pricex100 = (uint64_t)_app_display_data[n_idx].data.unit_price;
                display_data[n_idx].fuel_type = _app_display_data[n_idx].type;
                display_data[n_idx].start_stop = nozzle_state;

                // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "IN Sanki 6 D%d: S=%d, U=%d, P=%d, V=%d", 
                //     n_idx+1, nozzle_state, _app_display_data[n_idx].data.unit_price, 
                //     _app_display_data[n_idx].data.total_price, _app_display_data[n_idx].data.volume_l);
                // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "OUT Sanki 6 D%d: S=%d, U=%llu, P=%llu, V=%llu", 
                //     n_idx+1, display_data[n_idx].start_stop, display_data[n_idx].unit_pricex100, 
                //     display_data[n_idx].total_pricex100, display_data[n_idx].volume_lx1000);                    

                bool is_valid = sanki6_process_data(&display_data[n_idx]);
                if(is_valid)
                {
                    // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Sanki 6 D%d: Is Valid", n_idx+1);
                    sanki6_data_validate(&display_data[n_idx], n_idx);
                    is_pumped[n_idx] = sanki6_process_state_machine(&display_data[n_idx], n_idx);
                    // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Sanki 6 D%d: Is Pumped = %d", n_idx+1, is_pumped[n_idx]);
                }
            }
            else if((pal_time_get_ms() - ts_last_data_received[n_idx]) > 250)
            {
                // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Sanki 6 D%d: Timeout", n_idx+1);
                bool is_valid = sanki6_process_data(&display_data[n_idx]);
                if(is_valid)
                {
                    // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Sanki 6 D%d: Is Valid", n_idx+1);
                    is_pumped[n_idx] = sanki6_process_state_machine(&display_data[n_idx], n_idx);
                }
            }
            
            if(is_pumped[n_idx])
            {
                LOG_MSG_DEBUG(FUEL_INFO_EN, "Event Pumped NID = %d", n_idx);
                nozzle_event_t ne = {0};
                bool is_ready = sanki6_get_event(&ne, n_idx);
                if(is_ready)
                {
                    _on_event(APP_FUEL_EVENT_PUMPED, &ne);
                }
            }            
        }
        break;

        default:
        break;
    }
}

void on_nozzle1_start(){
    // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "on_nozzle1_start");  
    hsys_event_group_set_bits(_app_event, APP_FUEL_EVENT_NOZZLE1_START);
    if(_wake){
        // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Wake From Hsys WiFi");
        _wake(_wake_context);
    }
}

void on_nozzle1_stop(){
    // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "on_nozzle1_stop");  
    hsys_event_group_set_bits(_app_event, APP_FUEL_EVENT_NOZZLE1_STOP);
    if(_wake){
        // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Wake From Hsys WiFi");
        _wake(_wake_context);
    }
}

void on_nozzle2_start(){
    LOG_MSG_DEBUG(FUEL_DEBUG_EN, "on_nozzle2_start");     
    hsys_event_group_set_bits(_app_event, APP_FUEL_EVENT_NOZZLE2_START);
    if(_wake){
        // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Wake From Hsys WiFi");
        _wake(_wake_context);
    }
}

void on_nozzle2_stop(){
    LOG_MSG_DEBUG(FUEL_DEBUG_EN, "on_nozzle2_stop");   
    hsys_event_group_set_bits(_app_event, APP_FUEL_EVENT_NOZZLE2_STOP);
    if(_wake){
        // LOG_MSG_DEBUG(FUEL_DEBUG_EN, "Wake From Hsys WiFi");
        _wake(_wake_context);
    }
}
