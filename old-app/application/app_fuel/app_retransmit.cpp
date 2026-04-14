
#include <string.h>

#include "pal_logger.h"
#include "hsys_event.h"
#include "hsys_queue.h"

#include "app_retransmit.h"
#include "app_cloud.h"
#include "app.h"

#include "utils/utils.hpp"
#include "retransmission_manager.h"
#include "board.h"
#include "pumps/nozzle_event.h"
#include "hsys_queue.h"

#define __TAG__  "APP_RTXM"

#define APP_RTXM_DEBUG_LOG_EN      LOG_DIS

#define APP_RETXMIT_EVENT_CONFIG_READY          (0x1 << 0)
#define APP_RETXMIT_EVENT_SD_CARD_READY         (0x1 << 1)
#define APP_RETXMIT_EVENT_PUMP_EVENT_FAILED     (0x1 << 2)
#define APP_RETXMIT_EVENT_PROCESS_RETRY         (0x1 << 3)

#define FAILED_EVENT_QUEUE_SIZE                 5

// Structure to store failed event data
typedef struct {
    nozzle_event_t nozzle_event;
    retx_event_type_t event_type;
} failed_event_item_t;

static bool _is_initialized = false;
static fp_app_retransmission_on_event_t _on_event;
static fp_get_storage_interface_t _get_storage_interface;

// Queue to store failed events
static hsys_queue_handle_t _failed_event_queue = {0};
static failed_event_item_t _failed_event_queue_storage[FAILED_EVENT_QUEUE_SIZE];

static hsys_eventgroup_handle_t _app_event;
static retx_manager_t _retx_mgr;
static storage_interface_t *_storage = nullptr;

static fp_wake_task_t _wake;
static void * _wake_context;

static void _on_cloud_event(app_cloud_event_t event, void * arg);
static void _on_sdstatus_event(app_sd_event_t event, void * arg);

static void _on_sdstatus_event(app_sd_event_t event, void * arg)
{
    switch(event)
    {
        case APP_SD_EVNT_SD_READY:
            LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "SD card ready event received");
            hsys_event_group_set_bits(_app_event, APP_RETXMIT_EVENT_SD_CARD_READY);
            break;
        default:
            break;
    }

    if(_wake){
        _wake(_wake_context);
    }
}


// Cloud send callback for retransmission manager
static int32_t _retx_send_callback(retx_event_type_t type, const char* payload, size_t payload_len, void* user_data);

// Helper to get current date string (YYYYMMDD)
static void _get_date_key(char *out_buffer, size_t max_len) {
    // TODO: Implement actual date retrieval from RTC or NTP
    // For now, use a placeholder
    // extern void board_get_date_string(char *buffer, size_t max_len);
    // board_get_date_string(out_buffer, max_len);
    
}

void app_retransmit_init(const app_retransmit_init_t * p_retransmit_init){

    if(p_retransmit_init->fp_app_retransmission_on_event == NULL){
        LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Critical Error! : button default");
        while (1);
    }

    if(NULL == p_retransmit_init->app_init.fp_wake || NULL == p_retransmit_init->app_init.wake_context){
        LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    if(NULL == p_retransmit_init->fp_get_storage_interface){
        LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Critical Error! : fp_get_storage_interface is NULL");
        while (1);
    }

    _on_event = p_retransmit_init->fp_app_retransmission_on_event;
    _get_storage_interface = p_retransmit_init->fp_get_storage_interface;
    
    _wake = p_retransmit_init->app_init.fp_wake;
    _wake_context = p_retransmit_init->app_init.wake_context;
    
    _app_event = hsys_event_group_create();
    p_retransmit_init->app_init.event_table->on_cloud_event = (fp_event_interface_t)_on_cloud_event;
    p_retransmit_init->app_init.event_table->on_sd_event = (fp_event_interface_t)_on_sdstatus_event;

    // Initialize failed event queue
    hsys_queue_init(&_failed_event_queue, FAILED_EVENT_QUEUE_SIZE, sizeof(failed_event_item_t));

    // Initialize retransmission manager (storage will be set later when SD is ready)
    _storage = nullptr;
    _is_initialized = true;

    LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "app_retransmit_init : initialized");
}

static int32_t _retx_send_callback(retx_event_type_t type, const char* payload, size_t payload_len, void* user_data) {
    LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Retransmitting event type: %d, len: %d", (int)type, payload_len);
    
    // TODO: Call appropriate cloud send function based on type
    // For now, return success to test the flow
    switch(type) {
        case RETX_EVENT_TYPE_PUMPED:
            // return cube_sphere_send_pumped(payload, payload_len);
            LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Would send PUMPED event");
            break;
        case RETX_EVENT_TYPE_PRINTED:
            // return cube_sphere_send_printed(payload, payload_len);
            LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Would send PRINTED event");
            break;
        default:
            return -1;
    }
    
    return 0; // Success for testing
}


static void _on_cloud_event(app_cloud_event_t event, void * arg){
    
    switch(event)
    {
        case APP_CLOUD_EVNT_FUEL_PUMPED_FAILED:
            LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Cloud event failed, storing for retransmission");
            
            // Store the failed event data if provided
            if (arg != nullptr) {
                nozzle_event_t* ne = (nozzle_event_t*)arg;
                failed_event_item_t failed_item;
                
                // Use memcpy to copy the nozzle event
                memcpy(&failed_item.nozzle_event, ne, sizeof(nozzle_event_t));
                failed_item.event_type = RETX_EVENT_TYPE_PUMPED;
                
                // Add to queue
                if (hsys_queue_send(&_failed_event_queue, &failed_item, 0)) {
                    LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Stored failed nozzle event: idx=%d, vol=%u, price=%u", 
                        ne->n_idx, ne->volume_lx1000, ne->total_pricex100);
                    
                    hsys_event_group_set_bits(_app_event, APP_RETXMIT_EVENT_PUMP_EVENT_FAILED);
                } else {
                    LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Failed event queue is full! Event lost.");
                }
            } else {
                LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Failed event received but arg is NULL");
            }
        break;

        default:
            LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Unhandled cloud event: %d", (int)event);
        break;
    }
    
    if(_wake){
        _wake(_wake_context);
    }
}

typedef enum {
    APP_RETXMIT_STATE_WAITING = 0,
    APP_RETXMIT_STATE_INIT_RETX_MGR,
    APP_RETXMIT_STATE_RUNNING,
} app_retransmit_state_t;

void app_retransmit_run(){

    if(!_is_initialized)
    {
        return;
    }

    static app_retransmit_state_t rtx_state = APP_RETXMIT_STATE_WAITING;
    static uint32_t events;
    static uint32_t last_process_time = 0;

    switch(rtx_state)
    {
        case APP_RETXMIT_STATE_WAITING:
            LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Retransmission waiting for SD card");
            events = hsys_event_group_wait_bits(_app_event, 
                APP_RETXMIT_EVENT_SD_CARD_READY,
                FALSE, FALSE, 0);
            
            if(IS_EVENT(events, APP_RETXMIT_EVENT_SD_CARD_READY)) {
                hsys_event_group_clear_bits(_app_event, APP_RETXMIT_EVENT_SD_CARD_READY);
                rtx_state = APP_RETXMIT_STATE_INIT_RETX_MGR;
            }
        break;

        case APP_RETXMIT_STATE_INIT_RETX_MGR:
        {
            LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Initializing retransmission manager");
            
            // TODO: Get storage interface from SD module
            _get_storage_interface(&_storage);
            
            if (_storage != nullptr) {
                retx_manager_config_t config = {
                    .list_config = {
                        .storage = _storage,
                        .parent_path = "/retx",
                        .file_prefix = "evt",
                        .max_lines_per_file = 500,
                        .max_tracked_days = 7
                    },
                    .send_callback = _retx_send_callback,
                    .user_data = nullptr,
                    .retry_interval_ms = 60000  // 1 minute between retries
                };
                
                int32_t ret = retx_mgr_init(&_retx_mgr, &config);
                if (ret == 0) {
                    LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Retransmission manager initialized");
                    rtx_state = APP_RETXMIT_STATE_RUNNING;
                } else {
                    LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Failed to initialize retransmission manager: %d", ret);
                }
            } else {
                LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Storage interface not available");
            }
        }
        break;

        case APP_RETXMIT_STATE_RUNNING:
        {
            events = hsys_event_group_wait_bits(_app_event, 
                APP_RETXMIT_EVENT_PUMP_EVENT_FAILED | APP_RETXMIT_EVENT_PROCESS_RETRY,
                FALSE, FALSE, 0);
            
            if(IS_EVENT(events, APP_RETXMIT_EVENT_PUMP_EVENT_FAILED))
            {
                hsys_event_group_clear_bits(_app_event, APP_RETXMIT_EVENT_PUMP_EVENT_FAILED);
                
                // Process all pending failed events in the queue
                failed_event_item_t failed_item;
                while (hsys_queue_receive(&_failed_event_queue, &failed_item, 0))
                {
                    char date_key[16];
                    _get_date_key(date_key, sizeof(date_key));
                    
                    // Serialize nozzle_event_t to binary payload
                    const char* payload = (const char*)&failed_item.nozzle_event;
                    size_t payload_len = sizeof(nozzle_event_t);
                    
                    int32_t ret = retx_mgr_add_failed_event(
                        &_retx_mgr, 
                        date_key,
                        failed_item.event_type,
                        payload,
                        payload_len
                    );
                    
                    if (ret == 0) {
                        LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Failed event added to retransmission queue: idx=%d, vol=%u", 
                            failed_item.nozzle_event.n_idx, 
                            failed_item.nozzle_event.volume_lx1000);
                    } else {
                        LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Failed to add event to retransmission queue: %d", ret);
                    }
                }
            }
            
            // Periodic retry processing
            extern unsigned long board_millis(void);
            unsigned long current_time = board_millis();
            
            if ((current_time - last_process_time) >= 30000) // Every 30 seconds
            {
                last_process_time = current_time;
                
                int32_t ret = retx_mgr_process(&_retx_mgr);
                
                if (ret == 0) {
                    LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Event retransmitted successfully");
                } else if (ret == -4) {
                    // No pending events
                    LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "No pending retransmission events");
                } else if (ret == -5) {
                    LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Retransmission failed, will retry later");
                } else if (ret == -6) {
                    // Too soon to retry
                } else {
                    LOG_MSG_ERROR(APP_RTXM_DEBUG_LOG_EN, "Retransmission error: %d", ret);
                }
                
                // Get statistics
                uint32_t pending_count = 0;
                retx_mgr_get_pending_count(&_retx_mgr, &pending_count);
                if (pending_count > 0) {
                    LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Pending retransmission events: %u", pending_count);
                }
            }
        }
        break;

        default:
            LOG_MSG_DEBUG(APP_RTXM_DEBUG_LOG_EN, "Unhandled retransmission state");
            rtx_state = APP_RETXMIT_STATE_WAITING;
        break;
    }
}