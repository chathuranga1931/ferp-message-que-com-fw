

#include <cstring>

#include "app.h"
#include "app_config.h"
#include "board.h"

#include "hsys_config.h"
#include "pal_logger.h"
#include "hsys_task.h"
#include "hsys_event.h"
#include "hsys_mutex.h"

#define __TAG__  "APP_CONF"

#define CONFIG_DEBUG_LOG_EN      LOG_EN
#define CONFIG_WARN_LOG_EN       LOG_EN
#define CONFIG_ERROR_LOG_EN      LOG_EN
#define CONFIG_INFO_LOG_EN       LOG_EN

#define FN_DEVICE_CONFIGURATIONS 	"DeviceConfigs.json"
#define DIR_CONFIGURATION			"Configs"

#define APP_CONFIG_EVENTS_READY_TO_START            (0x1 << 0)

app_config_t _app_config;
static hsys_mutex_handle_t _app_config_mutex_handle;

static fp_app_config_on_event_t _on_event;
static bool _is_initialized = false;
static config_handle_t _config_hndl;

static hsys_eventgroup_handle_t _app_config_events;

static fp_wake_task_t _wake;
static void * _wake_context;

void _app_config_load_default();
void _on_spiffs_event(app_spiffs_event_t event, void * arg);
void _app_config_save_config();
void _app_config_load();

// void app_config_init(config_t * config_table, uint16_t config_table_size, fp_app_config_on_event_t fp_app_config_on_event, event_table_t * event_table, fp_wake_task_t fp_wake, void * wake_context){
void app_config_init(const app_config_init_t * p_config_init){

    int32_t ret = ERROR_OK;
    do{

        if(p_config_init == NULL){ 
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!: fp_app_config_on_event is NULL");
            while(1);
        }

        if(p_config_init->fp_app_config_on_event == NULL){ 
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!: fp_app_config_on_event is NULL");
            while(1);
        }

        _on_event = p_config_init->fp_app_config_on_event;

        if(p_config_init->app_init.event_table == NULL){ 
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!: event_table is NULL");
            while(1);
        }

        if(p_config_init->config_table == NULL){ 
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!: config_table is NULL");
            while(1);
        }

        config_init_t config_init;
        config_init.table = p_config_init->config_table;
        config_init.config_size = p_config_init->config_table_size;
        ret = hsys_config_init(config_init, &_config_hndl);  
        if(ret != ERROR_OK){
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!. app_config_init: hsys_config_init failed %d", ret);
            while (1);
        }

        _app_config_events = hsys_event_group_create();
        if(_app_config_events == NULL){
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!. app_config_init: hsys_event_group_create failed");
            while (1);
        }

        _app_config_mutex_handle = hsys_mutex_create();
        if(_app_config_mutex_handle == NULL){
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!. app_config_init: hsys_mutex_create failed");
            while (1);
        }

        if(NULL == p_config_init->app_init.fp_wake || NULL == p_config_init->app_init.wake_context){
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error! : fp_wake is NULL");
            while (1);
        }

        _wake = p_config_init->app_init.fp_wake;
        _wake_context = p_config_init->app_init.wake_context;

        p_config_init->app_init.event_table->on_spiffs_event = (fp_event_interface_t)_on_spiffs_event;

        LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "app_config_init: Initialization done.");

        char config_jason[2048];
        size_t json_length = 0;
        ret = hsys_config_convert_to_json(&_config_hndl, config_jason, sizeof(config_jason), &json_length);        
        if(ret != ERROR_OK){
            LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!. app_config_init: hsys_config_init failed");
            while (1);
        }
        LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "%s", config_jason);
        //logger.log_str(String(__func__) + "[" + String(__LINE__) +"]" + config_jason);
        
        _is_initialized = true;

    }while(false);
}

int32_t app_config_get_config_json(char * config_json, uint32_t * config_size, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    do{
        if(!_is_initialized || (_app_config_mutex_handle == NULL)){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        uint8_t is_locked = hsys_mutex_try_lock(_app_config_mutex_handle, timeout_ms);
        if(is_locked){
            size_t size_temp = *config_size;
            ret = hsys_config_convert_to_json(&_config_hndl, config_json, size_temp, &size_temp);
            *config_size = (uint32_t)size_temp;
            hsys_mutex_unlock(_app_config_mutex_handle);
        }
        else{
            LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Failed to lock the config data");
        }


    }while(false);
    return ret;
}

int32_t app_config_set_config_json(char * config_json, uint32_t config_size, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    do{
        if(!_is_initialized){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        uint8_t is_locked = hsys_mutex_try_lock(_app_config_mutex_handle, timeout_ms);
        if(is_locked){            
            ret = hsys_config_load_from_json(&_config_hndl, config_json, config_size);
            _app_config_save_config();
            hsys_mutex_unlock(_app_config_mutex_handle);
        }
        
    }while(false);
    return ret;
}


int32_t app_config_set(const char * config_name, const void * config_value, uint16_t bytes, hsys_type_t type, uint32_t timeout_ms){
    
    int32_t ret = ERROR_APP_BUSY;

    if((!_is_initialized)
        || (config_name == nullptr)
        || (config_value == nullptr) )
    {
        return ERROR_APP_NOT_INITIALIZED;
    }

    uint8_t is_locked = hsys_mutex_try_lock(_app_config_mutex_handle, timeout_ms);
    if(is_locked){
        for(int i=0; i<_config_hndl.config_size; i++){            
            
            if(strcmp(_config_hndl.table[i].name, config_name) == 0){
                if(_config_hndl.table[i].p_global_value == nullptr){
                    ret = ERROR_APP_INVALID_ARGUMENTS;
                    LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "%s: Not assigned global variable", config_name);
                    break;
                }
                else{
                    if(_config_hndl.table[i].max_length < bytes){
                        bytes = _config_hndl.table[i].max_length;
                    }
                    memcpy(_config_hndl.table[i].p_global_value, config_value, bytes);                             
                    ret = ERROR_OK;
                }
                break;
            }
        }
        hsys_mutex_unlock(_app_config_mutex_handle);
    }    
    
    return ret;   
}


int32_t app_config_get(const char * config_name, void * config_value, uint16_t * bytes, hsys_type_t * type, uint32_t timeout_ms){
    
    int32_t ret = ERROR_APP_BUSY;

    if((!_is_initialized)
        || (config_name == nullptr)
        || (config_value == nullptr) 
        || (type == nullptr) 
        || (bytes == nullptr) )
    {
        return ERROR_APP_INVALID_ARGUMENTS;
    }

    uint8_t is_locked = hsys_mutex_try_lock(_app_config_mutex_handle, timeout_ms);
    if(is_locked){
        for(int i=0; i<_config_hndl.config_size; i++){            
            
            if(strcmp(_config_hndl.table[i].name, config_name) == 0){
                if(_config_hndl.table[i].p_global_value == nullptr){
                    ret = ERROR_APP_BUSY;
                    LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "%s: Not assigned global variable", config_name);
                    break;
                }
                else{  
                    if(_config_hndl.table[i].max_length < *bytes){
                        *bytes = _config_hndl.table[i].max_length;
                    }                  
                    memcpy(config_value, _config_hndl.table[i].p_global_value, *bytes);
                    *type = _config_hndl.table[i].type; 
                    ret = ERROR_OK;
                }

                break;
            }
        }
        hsys_mutex_unlock(_app_config_mutex_handle);
    }    
    
    return ret;   
}

int32_t app_config_get_config(const char * config_name, void * config_value, uint32_t timeout_ms){

    int32_t ret = ERROR_APP_BUSY;

    if((!_is_initialized)
        || (config_name == nullptr)
        || (config_value == nullptr) )
    {
        return ERROR_APP_NOT_INITIALIZED;
    }

    uint8_t is_locked = hsys_mutex_try_lock(_app_config_mutex_handle, timeout_ms);
    if(is_locked){
        for(int i=0; i<_config_hndl.config_size; i++){            
            
            if(strcmp(_config_hndl.table[i].name, config_name) == 0){
                if(_config_hndl.table[i].p_global_value == nullptr){
                    ret = ERROR_APP_BUSY;
                    LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "%s: Not assigned global variable", config_name);
                    break;
                }
                else{
                    memcpy(config_value, _config_hndl.table[i].p_global_value, _config_hndl.table[i].max_length);
                    ret = ERROR_OK;
                    switch(_config_hndl.table[i].type){
                        case HSYS_TYPE_STRING:
                            LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "%s: %s", config_name, (char*)config_value);
                            break;
                        case HSYS_TYPE_BOOL:
                            LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "%s: %d", config_name, *(bool *)config_value);
                            break;
                        case HSYS_TYPE_UINT32:
                            LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "%s: %d", config_name, *(uint32_t*)config_value);
                            break;
                    }
                }

                break;
            }
        }
        hsys_mutex_unlock(_app_config_mutex_handle);
    }    
    
    return ret;    
}


void _on_spiffs_event(app_spiffs_event_t event, void * arg){
    switch(event){
        case APP_SPIFFS_EVNT_SPIFFS_READY:
            LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "SPIFFS ready");
            if(_app_config_events && _is_initialized)
            {                
                hsys_event_group_set_bits(_app_config_events, APP_CONFIG_EVENTS_READY_TO_START);
            }
        break;
    }

    if(_wake){
        _wake(_wake_context);
    }
}

void _app_config_save_config(){

    int32_t ret;
    
    // Use static buffers to avoid stack overflow
    static char config_jason[2048];
    size_t json_length = 0;
    char file_path[256];
    
    memset(config_jason, 0, sizeof(config_jason));  // Clear buffer first
    snprintf(file_path, sizeof(file_path), "%s/%s", DIR_CONFIGURATION, FN_DEVICE_CONFIGURATIONS);
    
    LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Converting config to JSON...");

    ret = hsys_config_convert_to_json(&_config_hndl, config_jason, sizeof(config_jason), &json_length);
    if(ret != ERROR_OK){
        LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!. app_config_init: hsys_config_init failed %d", ret);
        while (1);
    }
    
    LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "JSON conversion done, length: %d", (int)json_length);
    
    // Ensure null termination
    if(json_length < sizeof(config_jason)){
        config_jason[json_length] = '\0';
    } else {
        config_jason[sizeof(config_jason) - 1] = '\0';
        json_length = sizeof(config_jason) - 1;
    }
    
    // Yield to allow watchdog task to run before file write
    hsys_task_delay(10);
    
    LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Writing config to file: %s (length: %d)", file_path, (int)json_length);

    ret = app_spiffs_write_file(file_path, config_jason, 5000);            
    if(ret != ERROR_OK){
        LOG_MSG_ERROR(CONFIG_DEBUG_LOG_EN, "Critical Error!. app_config_init: app spiffs write failed");
        while (1);
    }
}

void _app_config_load(){

    // Use static buffers to avoid stack overflow
    static char configuration_json[2048];
    char file_path[256];
    snprintf(file_path, sizeof(file_path), "%s/%s", DIR_CONFIGURATION, FN_DEVICE_CONFIGURATIONS);
    
    uint32_t read_size = sizeof(configuration_json);
    int32_t ret = app_spiffs_read_file(file_path, configuration_json, &read_size, 5000);
    if(ret != ERROR_OK){
        LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Config file read failed, writing defualt config");      
        _app_config_save_config();
    }
    else if(read_size <= 10)
    {
        LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Config file is empty, writing defualt config");
        _app_config_save_config();
    }
    else
    {
        // //logger.log_str(configuration_json);
        ret = hsys_config_load_from_json(&_config_hndl, configuration_json, read_size);
        if(ret != ERROR_OK){
            LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Config conversion failed, reload defualt configs");
            _app_config_save_config();
        }
        else{
            LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Config loaded");
        }
    } 
}

typedef enum
{
    config_state_wait_for_config,
    config_state_wait_idle,
}config_state_t;

void app_config_run(){

    if(!_is_initialized)
    {
        return;
    }

    static config_state_t state = config_state_wait_for_config;
    static uint32_t event;

    switch(state)
    {
        case config_state_wait_for_config:
            event = hsys_event_group_wait_bits(_app_config_events, APP_CONFIG_EVENTS_READY_TO_START, true, false, 0);
            if(event == APP_CONFIG_EVENTS_READY_TO_START)
            {
                _app_config_load();
                
                LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Configuration Loaded"); 
                state = config_state_wait_idle;
                
                if(_on_event){
                    _on_event(APP_CONFIG_EVENT_LOADED, nullptr);
                }
            }
        break;

        case config_state_wait_idle:
        break;
    }    
}

int32_t app_config_get_display_type(uint32_t * dt, uint32_t timeout_ms)
{
    int32_t ret = ERROR_APP_INVALID_ARGUMENTS;

    LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Getting Display Type from Config");

    if(dt == nullptr){
        return ret;
    }

    ret = app_config_get_config("display_type", dt, timeout_ms);
    *dt = 6; //TODO Hardcoded to Sanki
    if(ret != ERROR_OK){
        goto exit;
    }

    LOG_MSG_DEBUG(CONFIG_DEBUG_LOG_EN, "Display Type: %d", *dt);
    
    ret = ERROR_OK;
    exit:
    return ret;
}

int32_t app_config_get_mac_str(char* mac, uint32_t timeout_ms)
{
    if (mac == nullptr) {
        return ERROR_APP_INVALID_ARGUMENTS;
    }

    uint8_t mac_address[6] = {0};
    board_get_mac_address(mac_address, sizeof(mac_address));

    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac_address[0], mac_address[1], mac_address[2],
            mac_address[3], mac_address[4], mac_address[5]);

    return ERROR_OK;
}