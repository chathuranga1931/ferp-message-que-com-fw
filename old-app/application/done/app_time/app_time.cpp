    

#include "stdint.h"
#include "stdbool.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "hsys_event.h"
#include "hsys_soft_timer.h"
#include "pal_logger.h"

#include "utils/utils.hpp"

#include "pal_ntp.h"
#include "pal_time.h"
#include "app_time.h"
#include "app_internet.h"

#include "ArduinoJson.h"

#define __TAG__  "APP_TMAN"

#define TIME_DEBUG_LOG_EN      LOG_DIS
#define TIME_WARN_LOG_EN       LOG_DIS
#define TIME_ERROR_LOG_EN      LOG_DIS
#define TIME_INFO_LOG_EN       LOG_DIS

#define APP_TIMEMAN_EVENTS_FILESYSTEM_READY        (0x1 << 0)
#define APP_TIMEMAN_EVENTS_INTERNET_CONNECTED      (0x1 << 1)
#define APP_TIMEMAN_EVENTS_BACKUP_TIME             (0x1 << 2)


// Month names in flash
const static char monthNames_P[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
// Day of the week names in flash
const static char dayNames_P[] = "SunMonTueWedThuFriSat";

static hsys_eventgroup_handle_t _app_timeman_events;
static fp_wake_task_t _wake;
static void * _wake_context;
static bool _is_initialized = false;
static fp_app_timeman_on_event_t _on_event;
static hsys_timer_handle_t _periodic_timer;   
static char content[256] = {0}; 

static const timeman_configs_t * _p_timeman_configs = NULL;

static void on_config_event(app_config_event_t event, void * arg);
static void on_internet_event(app_internet_event_t event, void * arg);
static void _timer_callback(void * arg);

int32_t app_timeman_init(const timeman_init_t * p_timeman_init){

    int32_t ret = ERROR_OK;

    if(p_timeman_init == NULL){
        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Critical Error!. Internet Monitor Null pointer reference, please check.., Critical Error");
        while (1);
    }
    
    // Initialize system time to a valid baseline (2020-01-01) to prevent issues with NTP
    // This must be done BEFORE any RTC reads or time operations
    time_t now;
    time(&now);
    if (now < 1577836800) { // Before 2020-01-01 00:00:00 UTC
        LOG_MSG_INFO(TIME_DEBUG_LOG_EN, "Initializing system time to 2020-01-01 baseline");
        struct timeval tv = {
            .tv_sec = 1577836800,  // 2020-01-01 00:00:00 UTC
            .tv_usec = 0
        };
        settimeofday(&tv, NULL);
    }
    
    _on_event = p_timeman_init->fp_app_timeman_on_event;

    if(p_timeman_init->app_init.event_table == NULL){
        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "WIFI Null pointer reference, please check.., Critical Error");
        while (1);        
    }

    _app_timeman_events = hsys_event_group_create();


    if(NULL == p_timeman_init->app_init.fp_wake || NULL == p_timeman_init->app_init.wake_context){
        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Critical Error! : fp_wake is NULL");
        while (1);
    }

    if(NULL == p_timeman_init->p_configs){
        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Critical Error! : p_configs is NULL");
        while (1);
    }

    _wake = p_timeman_init->app_init.fp_wake;
    _wake_context = p_timeman_init->app_init.wake_context;
    _p_timeman_configs = p_timeman_init->p_configs;

    p_timeman_init->app_init.event_table->on_config_event = (fp_event_interface_t)on_config_event;
    p_timeman_init->app_init.event_table->on_internet_event = (fp_event_interface_t)on_internet_event;    
    _periodic_timer = hsys_timer_create("Periodic Timer", 15000, true, (void *)NULL, _timer_callback);

    bool is_timer_started = hsys_start_timer(_periodic_timer);
    if(!is_timer_started){
        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to start periodic timer");
    }

    _is_initialized = true;
    LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Timemanager : initialized");

    return ret;
}

void on_internet_event(app_internet_event_t event, void * arg){

    switch(event)
    {
        case APP_INTERNET_EVENT_CONNECTED:
            LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Internet connected");
            hsys_event_group_set_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_INTERNET_CONNECTED);
        break;

        default:
        break;
    }

    if(_wake)
    {
        LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Wake Task from Internet Event");
        _wake(_wake_context);
    }
}

void on_config_event(app_config_event_t event, void * arg){

    switch(event)
    {
        case APP_CONFIG_EVENT_LOADED:
            LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Config loaded");
            hsys_event_group_set_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_FILESYSTEM_READY);
        break;

        default:
        break;
    }

    if(_wake)
    {
        LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Wake Task from Config Event");
        _wake(_wake_context);
    }
}

typedef enum {
    TIMEMAN_STATE_WAITING_FOR_FILESYSTEM,
    TIMEMAN_STATE_LOAD_FROM_BACKUP_FILE,
    TIMEMAN_STATE_FAILED_TO_LOAD_FROM_BACKUP_FILE,
    TIMEMAN_STATE_SUCCESS_LOAD_FROM_BACKUP_FILE,

    TIMEMAN_STATE_LOAD_FROM_RTC,
    TIMEMAN_STATE_FAILED_LOAD_FROM_RTC,
    TIMEMAN_STATE_SUCCESS_LOAD_FROM_RTC,
    
    TIMEMAN_STATE_WAITING_FOR_INTERNET,
    TIMEMAN_STATE_SYNC_FROM_NTP_START,
    TIMEMAN_STATE_SYNC_FROM_NTP_WAITING,
    TIMEMAN_STATE_FAILED_TO_SYNC_FROM_NTP,
    TIMEMAN_STATE_SUCCESS_SYNC_FROM_NTP,

    TIMEMAN_STATE_UPDATE_RTC,
    TIMEMAN_STATE_UPDATE_BACKUP_FILE,

    TIMEMAN_STATE_READY,
} timeman_state_t;

bool timeman_update_backup_time(time_t time){

    JsonDocument root;

    memset(content, 0, sizeof(content)); // Clear the content buffer
    root["time"] = time;
    serializeJson(root, content, sizeof(content));
    size_t content_size = strlen(content);

    file_operation_config_t file_config;
    file_config.filepath = _p_timeman_configs->local_backup_filepath;
    file_config.content = content;
    file_config.content_size = &content_size;
    file_config.timeout_ms = 1000;
    sprintf(file_config.mode, "w"); // Open file in write mode

    LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Writing to backup file: %s", file_config.filepath);

    bool status = _p_timeman_configs->spiffs->fp_write(&file_config);
    if(!status){
        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Backup file write failed");        
    }

    return status;
}

bool timeman_read_backup_time(time_t * time)
{
    void * file_handler; 
    bool status = false;
    size_t content_size = sizeof(content) - 1; // Reserve space for null terminator
    memset(content, 0, sizeof(content)); // Clear the content buffer

    file_operation_config_t file_config;
    file_config.filepath = _p_timeman_configs->local_backup_filepath;
    file_config.content = content;
    file_config.content_size = &content_size;
    file_config.timeout_ms = 1000;
    sprintf(file_config.mode, "r"); // Open for reading and writing
    
    status = _p_timeman_configs->spiffs->fp_read(&file_config);
    if(!status)
    {
        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to read backup file");
        return status;
    }

    LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Time: %s", content);

    JsonDocument root;
    deserializeJson(root, content, DeserializationOption::NestingLimit(20));

    if(root.containsKey("time"))
    {
        *time = root["time"].as<long>();
    }
    else
    {
        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "No time found in backup file");
        status = false; // Indicate failure
    }

    return status;
}

void app_timeman_run(void)
{
    if(!_is_initialized)
    {
        return;
    }

    static uint32_t events = 0;
    static uint32_t last_events = 0;
    static timeman_state_t state = TIMEMAN_STATE_WAITING_FOR_FILESYSTEM;
    static time_t time_ntp;
    static time_t time_rtc; 
    static time_t last_working_time;   
    static bool status;
    static timeman_state_t prev_state = state;
    static uint32_t ts_state_changed = 0; 
    static uint64_t current_time_ms;

    typedef enum {
        TIME_LOAD_STATUS_NONE = 0,
        TIME_LOAD_STATUS_BACKUP_FILE = (1 << 0),
        TIME_LOAD_STATUS_RTC = (1 << 1),
        TIME_LOAD_STATUS_NTP = (1 << 2),
    } time_load_status_bm_t;

    static uint32_t time_load_status_bm = 0;  

    if(state != prev_state)
    {
        LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Time Manager State Changed: %d -> %d", prev_state, state);
        prev_state = state;        
        ts_state_changed = pal_time_get_ms();
    }

    LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Time Manager : %d", state);

    bool is_loop_once = true;    
    while(is_loop_once)
    {
        is_loop_once = false;
        current_time_ms = pal_time_get_ms();

        switch(state)
        {
            case TIMEMAN_STATE_WAITING_FOR_FILESYSTEM:
            {    
                events = hsys_event_group_wait_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_FILESYSTEM_READY, false, false, 0);
                if(IS_EVENT(events, APP_TIMEMAN_EVENTS_FILESYSTEM_READY))
                {
                    hsys_event_group_clear_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_FILESYSTEM_READY);
                    LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Filesystem is ready");
                    state = TIMEMAN_STATE_LOAD_FROM_BACKUP_FILE;
                    time_load_status_bm = TIME_LOAD_STATUS_NONE;
                    is_loop_once = true; // Loop again to process the next state
                }
            }
            break;

            case TIMEMAN_STATE_LOAD_FROM_BACKUP_FILE:
            {
                status = timeman_read_backup_time(&last_working_time);
                if(!status){
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to read time from backup file");
                    state = TIMEMAN_STATE_FAILED_TO_LOAD_FROM_BACKUP_FILE;
                    is_loop_once = true; // Loop again to process the next state
                    break;
                }

                // Validate backup time (must be after 2020-01-01 00:00:00 UTC = 1577836800)
                if(last_working_time < 1577836800) {
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Backup time is invalid: %ld (before 2020), ignoring", last_working_time);
                    state = TIMEMAN_STATE_FAILED_TO_LOAD_FROM_BACKUP_FILE;
                    is_loop_once = true; // Loop again to process the next state
                    break;
                }

                LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Last working time loaded from backup file: %ld", last_working_time);
                _on_event(APP_TIMEMAN_READY, &last_working_time);
                
                time_load_status_bm |= TIME_LOAD_STATUS_BACKUP_FILE;
                state = TIMEMAN_STATE_SUCCESS_LOAD_FROM_BACKUP_FILE;
                is_loop_once = true; // Loop again to process the next state                         
            }
            break;

            case TIMEMAN_STATE_FAILED_TO_LOAD_FROM_BACKUP_FILE:
            {
                state = TIMEMAN_STATE_LOAD_FROM_RTC;
                LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to load time from backup file, trying to load from RTC");
                is_loop_once = true; // Loop again to process the next state
            }
            break;

            case TIMEMAN_STATE_SUCCESS_LOAD_FROM_BACKUP_FILE:
            {
                LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Successfully loaded time from backup file, waiting for RTC or NTP sync");
                state = TIMEMAN_STATE_LOAD_FROM_RTC;
                is_loop_once = true; // Loop again to process the next state
            }
            break;

            case TIMEMAN_STATE_LOAD_FROM_RTC:
            {
                status = _p_timeman_configs->rtc->fp_rtc_read_time(&time_rtc);
                if(!status){
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to get time from RTC");
                    state = TIMEMAN_STATE_FAILED_LOAD_FROM_RTC;
                    is_loop_once = true; // Loop again to process the next state
                    break;
                }

                // Validate RTC time (must be after 2020-01-01 00:00:00 UTC = 1577836800)
                if(time_rtc < 1577836800) {
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "RTC time is invalid: %ld (before 2020), ignoring", time_rtc);
                    state = TIMEMAN_STATE_FAILED_LOAD_FROM_RTC;
                    is_loop_once = true; // Loop again to process the next state
                    break;
                }

                time_load_status_bm |= TIME_LOAD_STATUS_RTC;
                timeman_update_backup_time(time_rtc);
                
                LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "RTC: %ld", time_rtc);

                struct timeval tv = {
                    .tv_sec  = time_rtc,
                    .tv_usec = 0
                };
                settimeofday(&tv, NULL);

                _on_event(APP_TIMEMAN_READY, &time_rtc);
                _on_event(APP_TIMEMAN_TIME_UPDATED_FROM_RTC, &time_rtc);

                state = TIMEMAN_STATE_WAITING_FOR_INTERNET;
                is_loop_once = true; // Loop again to process the next state
            }
            break;

            case TIMEMAN_STATE_FAILED_LOAD_FROM_RTC:
            {
                LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to load time from RTC, waiting for internet");
                state = TIMEMAN_STATE_WAITING_FOR_INTERNET;
                is_loop_once = true; // Loop again to process the next state
            }         
            break;   

            case TIMEMAN_STATE_WAITING_FOR_INTERNET:
            {
                events = hsys_event_group_wait_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_INTERNET_CONNECTED, false, false, 0);
                if(IS_EVENT(events, APP_TIMEMAN_EVENTS_INTERNET_CONNECTED))
                {
                    hsys_event_group_clear_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_INTERNET_CONNECTED);
                    LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Internet is connected");
                    state = TIMEMAN_STATE_SYNC_FROM_NTP_START;
                    is_loop_once = true; // Loop again to process the next state
                }
                else
                {
                    #define TIME_TO_WAIT_FOR_INTERNET_WHEN_NO_VALID_TIME 20000 // 20 seconds
                    static bool once = false;
                    if(!once && (current_time_ms - ts_state_changed) > TIME_TO_WAIT_FOR_INTERNET_WHEN_NO_VALID_TIME)
                    {
                        if(time_load_status_bm == TIME_LOAD_STATUS_BACKUP_FILE)
                        {
                            LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Waiting for internet connection timedout, spiffs time available: %ld", last_working_time);
                            struct timeval tv = { .tv_sec = last_working_time, .tv_usec = 0 };
                            settimeofday(&tv, NULL);

                            _on_event(APP_TIMEMAN_READY, &last_working_time);
                            _on_event(APP_TIMEMAN_TIME_UPDATED_FROM_BACKUP_FILE, &last_working_time);
                        }
                        else if(time_load_status_bm == TIME_LOAD_STATUS_NONE)
                        {
                            LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Waiting for internet connection timedout, no valid time source available");
                            _on_event(APP_TIMEMAN_TIME_UPDATE_FAILED_CRITICAL, NULL);
                        }
                        once = true; // Ensure this block only runs once
                    }
                }
            }
            break;

            case TIMEMAN_STATE_SYNC_FROM_NTP_START:
            {
                // Ensure system time is valid before initializing NTP
                time_t check_time;
                time(&check_time);
                if (check_time < 1577836800) {
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "System time is invalid before NTP init (%ld), setting to 2020", check_time);
                    struct timeval tv = { .tv_sec = 1577836800, .tv_usec = 0 };
                    settimeofday(&tv, NULL);
                    time(&check_time);
                    LOG_MSG_INFO(TIME_DEBUG_LOG_EN, "System time after correction: %ld", check_time);
                }
                
                bool status = _p_timeman_configs->ntp->fp_hsys_ntp_init_default();
                if(!status){
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to initialize NTP");
                    state = TIMEMAN_STATE_FAILED_TO_SYNC_FROM_NTP;
                    break;
                }

                status = _p_timeman_configs->ntp->fp_hsys_ntp_sync_start();
                if(!status){
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to sync time from NTP");
                    state = TIMEMAN_STATE_FAILED_TO_SYNC_FROM_NTP;
                    break;
                }

                state = TIMEMAN_STATE_SYNC_FROM_NTP_WAITING;                
                
            }
            break;

            case TIMEMAN_STATE_SYNC_FROM_NTP_WAITING:
            {
                status = _p_timeman_configs->ntp->fp_hsys_ntp_sync_process();
                if(!status){
                    uint32_t elapsed_time = pal_time_get_ms() - ts_state_changed;
                    if(elapsed_time > 60000)
                    {
                        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Timesync timeout");
                        state = TIMEMAN_STATE_FAILED_TO_SYNC_FROM_NTP;
                        break;
                    }
                    else
                    {
                        break;
                    }
                }

                status = _p_timeman_configs->ntp->fp_hsys_ntp_get_epochtime(&time_ntp);
                if(!status){
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to get time from NTP");
                    state = TIMEMAN_STATE_FAILED_TO_SYNC_FROM_NTP;

                    _on_event(APP_TIMEMAN_TIME_UPDATE_FAILED_FROM_NTP, &time_ntp);
                    status = _p_timeman_configs->ntp->fp_hsys_ntp_deinit();
                    if(!status){
                        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to deinitialize NTP");
                    }
                    break;
                }

                status = _p_timeman_configs->ntp->fp_hsys_ntp_deinit();
                if(!status){
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to deinitialize NTP");
                }
                
                LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "NTP: %ld", time_ntp);
                _on_event(APP_TIMEMAN_READY, &time_ntp);
                state = TIMEMAN_STATE_SUCCESS_SYNC_FROM_NTP;               
            }
            break;
 
            case TIMEMAN_STATE_FAILED_TO_SYNC_FROM_NTP:
            {
                LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to sync time from NTP, Retrying...");
                state = TIMEMAN_STATE_SYNC_FROM_NTP_START;       
            }
            break;

            case TIMEMAN_STATE_SUCCESS_SYNC_FROM_NTP:
            {
                LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Successfully synced time from NTP, updating RTC and backup file");
                state = TIMEMAN_STATE_UPDATE_RTC;
                is_loop_once = true; // Loop again to process the next state
            }
            break;

            case TIMEMAN_STATE_UPDATE_RTC:
            {
                _p_timeman_configs->rtc->fp_rtc_set_time(time_ntp);
                if(_p_timeman_configs->rtc->fp_rtc_read_time(&time_rtc)){
                    LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "RTC updated successfully: %ld", time_rtc);
                }
                else{
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to update RTC");
                }

                state = TIMEMAN_STATE_UPDATE_BACKUP_FILE;
                is_loop_once = true; // Loop again to process the next state
            }
            break;

            case TIMEMAN_STATE_UPDATE_BACKUP_FILE:
            {
                void * file_handler;
                status = timeman_update_backup_time(time_ntp);
                if(!status){
                    LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to update backup file");
                    state = TIMEMAN_STATE_READY; // Reset to ready state
                }

                hsys_start_timer(_periodic_timer);
                state = TIMEMAN_STATE_READY;
            }
            break;

            case TIMEMAN_STATE_READY:
                events = hsys_event_group_wait_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_BACKUP_TIME, false, false, 0);
                if(IS_EVENT(events, APP_TIMEMAN_EVENTS_BACKUP_TIME))
                {
                    int32_t ret = pal_ntp_get_epoch_time(&time_ntp);
                    if(ret != 0){
                        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to get time from NTP");
                        break;
                    }

                    hsys_event_group_clear_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_BACKUP_TIME);
                    LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Backup time event received");

                    status = timeman_update_backup_time(time_ntp);
                    if(!status){
                        LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Failed to update backup time");
                    }
                    else{
                        LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Backup time updated successfully");
                    }
                }
            break;

            default:
            {
                LOG_MSG_ERROR(TIME_DEBUG_LOG_EN, "Unknown state: %d", state);
                state = TIMEMAN_STATE_READY; // Reset to ready state
            }
            break;
        }
    }
}

void _timer_callback(void * arg)
{
    if(_wake)
    {
        LOG_MSG_DEBUG(TIME_DEBUG_LOG_EN, "Wake From Timer");
        hsys_event_group_set_bits(_app_timeman_events, APP_TIMEMAN_EVENTS_BACKUP_TIME);
        _wake(_wake_context);
    }
}

