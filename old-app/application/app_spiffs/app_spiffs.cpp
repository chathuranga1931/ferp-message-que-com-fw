
#include "app.h"
#include "pal_logger.h"
#include "hsys_mutex.h"
#include "hsys_task.h"
#include "hsys_event.h"
#include "pal/pal_spiffs.h"

#include <string.h>
#include <stdlib.h>

#define __TAG__  "APP_SPIF"

static bool _is_initialized = false;
static hsys_mutex_handle_t _spiffs_mutex_handle;
static fp_app_spiffs_on_event_t _on_event;
static hsys_task_handle_t app_spiffs_task_handle;
static hsys_eventgroup_handle_t _spiffs_events;

int32_t _lock_spiffs(uint32_t timeout_ms);
void _unlock_spiffs(void);

void app_spiffs_init(fp_app_spiffs_on_event_t fp_app_spiffs_on_event, event_table_t * event_table, int32_t priority){
    
    if(fp_app_spiffs_on_event == NULL){
        LOG_MSG_DEBUG(LOG_EN, "app_spiffs_init: fp_app_spiffs_on_event is NULL");
    }
    _on_event = fp_app_spiffs_on_event;

    _spiffs_mutex_handle = hsys_mutex_create();
    if(_spiffs_mutex_handle == NULL){
        LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_spiffs_init: hsys_mutex_create failed");
        while (1);
    }

    // Configure SPIFFS using PAL
    pal_spiffs_config_t spiffs_config = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    pal_spiffs_info_t spiffs_info = {};
    int32_t ret = pal_spiffs_init(&spiffs_config, &spiffs_info);
    
    if(ret != PAL_OK){
        LOG_MSG_ERROR(LOG_EN, "Failed to initialize SPIFFS via PAL");
        return;
    }

    LOG_MSG_DEBUG(LOG_EN, "SPIFFS initialized: Total=%zu bytes, Used=%zu bytes, Free=%zu bytes",
                  spiffs_info.total_bytes, spiffs_info.used_bytes, spiffs_info.free_bytes);

    _is_initialized = true;

    if(_on_event){
        _on_event(APP_SPIFFS_EVNT_SPIFFS_READY, NULL);
    }
}

// void app_spiffs_start(void){
//     if(_is_initialized && _spiffs_events){
//         hsys_event_group_set_bits(_spiffs_events, APP_SPIFFS_EVENTS_READY_TO_START);    
//     }    
// }

// void _app_spiffs_process(void * arg){
        
//     hsys_event_group_wait_bits(_spiffs_events, APP_SPIFFS_EVENTS_READY_TO_START, pdTRUE, pdFALSE, portMAX_DELAY);
    
//     LOG_MSG_DEBUG(LOG_EN, "[SPIFFS] Task started");
//     time_t last_working_time = 0;
//     int32_t ret = _app_spiffs_load_last_working_time(&last_working_time);
//     if(_on_event){
//         _on_event(APP_SPIFFS_EVNT_LAST_WRK_TIME_READY, (void *)(last_working_time));
//     }

//     while(true){ 
//         uint32_t events_waiting = (
//             APP_SPIFFS_EVENTS_UPDATE_LATEST_TIME
//         );
//         uint32_t app_event_bits = hsys_event_group_wait_bits(_spiffs_events, events_waiting, pdTRUE, pdFALSE, portMAX_DELAY);
        
//         if(app_event_bits & APP_SPIFFS_EVENTS_UPDATE_LATEST_TIME){
//             _app_spiffs_update_last_working_time();
//         }


//     }

//     hsys_task_delete(app_spiffs_task_handle);
// }

int32_t _lock_spiffs(uint32_t timeout_ms){
    if(_spiffs_mutex_handle){
        return hsys_mutex_try_lock(_spiffs_mutex_handle, timeout_ms); 
    } 
    return 0;
}

void _unlock_spiffs(void){
    if(_spiffs_mutex_handle){
        hsys_mutex_unlock(_spiffs_mutex_handle);
    }
}





// int32_t _app_spiffs_load_last_working_time(time_t * time){

//     int32_t ret = ERROR_APP_SPIFF_TIME_READ_FAIL;

//     do{

// 		String filename = FILE_NAME_LAST_ACTIVE_TIME;
// 		String file_path = FOLDER_NAME_FOR_ACTIVE_TIME + filename;
// 		if(!file_path.startsWith("/")){
// 			file_path = "/" + file_path;
//         }

//         int32_t is_locked = _lock_spiffs();
//         if(is_locked <= 0){
//             LOG_MSG_DEBUG(LOG_EN, "Failed to lock SPIFFS");
//             break;
//         }

//         bool is_exist = SPIFFS.exists(file_path);
//         if(!is_exist){
//             LOG_MSG_DEBUG(LOG_EN, "No " + file_path + " for reading");
// 			break;
//         }

//         File file = SPIFFS.open(file_path);
//         if(!file){
//             LOG_MSG_DEBUG(LOG_EN, "Failed to open " + file_path + " for reading");
// 			break;
//         }

//         if(file.size() > 256){
//             SPIFFS.remove(file_path.c_str());
// 			break;
//         }

//         String fileContent = "";
//         if(file.available()){
//             fileContent = file.readString();
//         }
//         file.close();
//         _unlock_spiffs();

// 		LOG_MSG_DEBUG(fileContent);

//         JsonDocument root;
// 		deserializeJson(root, fileContent, DeserializationOption::NestingLimit(20));
//         if(root.containsKey("time")){
//             *time = root["time"].as<long>();
//         }
// 		else{
//             *time = 0;
//             break;
// 		}

//         ret = ERROR_OK;
//         LOG_MSG_DEBUG(LOG_EN, "time loaded from last working : " + String(*time));

// 	}while(false);

//     _unlock_spiffs();
// 	return ret;
// }

// static struct timeval _last_working_time;
// int32_t app_spiffs_update_last_working_time(struct timeval now){
//     if(_is_initialized && _spiffs_events){
//         _last_working_time = now;
//         hsys_event_group_set_bits(_spiffs_events, APP_SPIFFS_EVENTS_UPDATE_LATEST_TIME);    
//         return ERROR_OK;
//     }

//     return ERROR_FAIL;
// }
// int32_t _app_spiffs_update_last_working_time(){

//     int32_t ret = ERROR_OK;

//     do{
//         JsonDocument root;
//         root["time"] = _last_working_time.tv_sec;

//         String jsonStr;
// 		serializeJson(root, jsonStr);
//         LOG_MSG_DEBUG(jsonStr);

//         String filename = FILE_NAME_LAST_ACTIVE_TIME;
//         String file_path = FOLDER_NAME_FOR_ACTIVE_TIME + filename;

//         int32_t is_locked = _lock_spiffs();
//         if(is_locked <= 0){
//             LOG_MSG_DEBUG(String(__func__) +  " : Failed to lock SPIFFS");
//             break;
//         }

//         File file = SPIFFS.open(file_path, FILE_WRITE);
//         if(!file){
//             LOG_MSG_DEBUG(LOG_EN, "Failed to open " + file_path + " for writing");
//             break;
//         }

//         if(file.print(jsonStr)){
//             // LOG_MSG_DEBUG(LOG_EN, "" + file_path + " written");
//         } else {
//             LOG_MSG_DEBUG(LOG_EN, "" + file_path + " failed");
//         }
//         file.close();

//     }while(false);

//     _unlock_spiffs();
//     return ret;
// }

// int32_t _app_spiffs_create_file_for_startup_config_in_spiffs(void){

//     int32_t ret = ERROR_OK;

//     do{

// 		String filename = FILE_NAME_STARTUP_LOG;
// 		String file_path = FOLDER_NAME_STARTUP_LOG + filename;
// 		if(!file_path.startsWith("/")){
// 			file_path = "/" + file_path;
//         }

// 		File file;
//         int32_t is_locked = _lock_spiffs();
//         if(is_locked <= 0){
//             LOG_MSG_DEBUG(String(__func__) + " : Failed to lock SPIFFS");
//             break;
//         }

//         if(!SPIFFS.exists(file_path)){
//             LOG_MSG_DEBUG(LOG_EN, "No " + file_path + " for reading");
// 			file = SPIFFS.open(file_path, FILE_WRITE);
//         }
// 		else{
// 			SPIFFS.remove(file_path);
// 			file = SPIFFS.open(file_path);
// 		}
// 		file.close();

//         _unlock_spiffs();
//         LOG_MSG_DEBUG(LOG_EN, "Created new file for startup logging...");

// 	}while(false);

//     _unlock_spiffs();
// 	return ret;
// }

// static String _startup_log_message = "";
// static uint32_t _startup_log_message_count = 0;
// int32_t add_line_to_startup_config_in_spiffs(String msg){
//     if(_is_initialized && _spiffs_events){
//         _startup_log_message = String(_startup_log_message_count++) + " : " +  msg;
//         hsys_event_group_set_bits(_spiffs_events, APP_SPIFFS_EVENTS_ADD_NEWL_STARTUP);    
//         return ERROR_OK;
//     }
//     return ERROR_FAIL;
// }
// int32_t _add_line_to_startup_config_in_spiffs(){

//     int32_t ret = ERROR_OK;

//     do{

// 		String filename = FILE_NAME_STARTUP_LOG;
// 		String file_path = FOLDER_NAME_STARTUP_LOG + filename;
// 		if(!file_path.startsWith("/"))
// 			file_path = "/" + file_path;

// 		File file;
//         int32_t is_locked = _lock_spiffs();
//         if(is_locked <= 0){
//             LOG_MSG_DEBUG(String(__func__) + " : Failed to lock SPIFFS");
//             break;
//         }

//         if(!SPIFFS.exists(file_path)){
//             LOG_MSG_DEBUG(LOG_EN, "No " + file_path + " for reading");
// 			file = SPIFFS.open(file_path, FILE_WRITE);
//         }
// 		else{
// 			file = SPIFFS.open(file_path, FILE_APPEND);
// 		}

// 		file.println(_startup_log_message);
// 		file.close();

//         _unlock_spiffs();

//         LOG_MSG_DEBUG(LOG_EN, "Added a line to startup log : " + _startup_log_message);

// 	}while(false);

//     _unlock_spiffs();
// 	return ret;
// }

int32_t app_spiffs_append_line(const char * file_path, const char * line, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_spiffs(timeout_ms);
        if(is_locked <= 0){
            LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
            ret = ERROR_APP_BUSY;
            break;
        }

        // Create line with newline
        static char line_with_newline[512];
        snprintf(line_with_newline, sizeof(line_with_newline), "%s\n", line);
        
        // Use PAL to append the line
        int32_t pal_ret = pal_spiffs_file_append(file_path, (const uint8_t *)line_with_newline, strlen(line_with_newline));
        if(pal_ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to append line to file: %s", file_path);
            ret = ERROR_FAILED;
        } else {
            LOG_MSG_DEBUG(LOG_EN, "Added a line to file: %s", line);
        }

        _unlock_spiffs();

    }while(false);
    
    return ret;
}

int32_t app_spiffs_create_file(const char * file_path, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;

    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_spiffs(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        // Use PAL to create the file
        int32_t pal_ret = pal_spiffs_file_create(file_path);
        if(pal_ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to create file: %s", file_path);
            ret = ERROR_FAILED;
        }

        _unlock_spiffs();

        
    }while(false);
    
    return ret;
}

int32_t app_spiffs_append_file(const char * file_path, const uint8_t * data, size_t len, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_spiffs(timeout_ms);
        if(is_locked <= 0){
            LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
            ret = ERROR_APP_BUSY;
            break;
        }
        
        // Use PAL to append the content
        int32_t pal_ret = pal_spiffs_file_append(file_path, data, len);
        if(pal_ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to append content to file: %s", file_path);
            ret = ERROR_FAILED;
        } else {
            LOG_MSG_DEBUG(LOG_EN, "Appended content to file: %s", file_path);
        }

        _unlock_spiffs();

    }while(false);
    
    return ret;
}

int32_t app_spiffs_delete_file(const char * file_path, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_spiffs(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        // Check if file exists before deleting
        bool exists = false;
        if(pal_spiffs_file_exists(file_path, &exists) == PAL_OK && exists){
            int32_t pal_ret = pal_spiffs_file_delete(file_path);
            if(pal_ret != PAL_OK){
                LOG_MSG_ERROR(LOG_EN, "Failed to delete file: %s", file_path);
                ret = ERROR_FAILED;
            }
        }
        
        _unlock_spiffs();

    }while(false);
    
    return ret;
}

int32_t app_spiffs_write_file(const char * file_path, const char * content, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_spiffs(timeout_ms);
        if(is_locked <= 0){
            LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
            ret = ERROR_APP_BUSY;
            break;
        }
        
        LOG_MSG_DEBUG(LOG_EN, "SPIFFS locked, content length check...");
        
        // Calculate content length
        size_t content_len = strlen(content);
        
        LOG_MSG_DEBUG(LOG_EN, "Content length: %d bytes", (int)content_len);
        
        // Allocate buffer for content with newline
        char * content_with_newline = (char *)malloc(content_len + 2); // +1 for \n, +1 for \0
        if(content_with_newline == NULL){
            LOG_MSG_ERROR(LOG_EN, "Failed to allocate memory for file write");
            ret = ERROR_FAILED;
            _unlock_spiffs();
            break;
        }
        
        snprintf(content_with_newline, content_len + 2, "%s\n", content);
        
        LOG_MSG_DEBUG(LOG_EN, "Writing %d bytes to file: %s", (int)strlen(content_with_newline), file_path);
        
        // Yield to allow watchdog task to run
        hsys_task_delay(10);
        
        // Use PAL to write the file
        int32_t pal_ret = pal_spiffs_file_write(file_path, (const uint8_t *)content_with_newline, strlen(content_with_newline));
        
        free(content_with_newline);
        
        if(pal_ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to write file: %s", file_path);
            ret = ERROR_FAILED;
        }

        _unlock_spiffs();

    }while(false);
    
    return ret;
}

int32_t app_spiffs_read_file(const char * file_path, char * content, uint32_t * read_size, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{

        if(_is_initialized == false){
            LOG_MSG_DEBUG(LOG_EN, "SPIFFS Not initialized");
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_spiffs(timeout_ms);
        if(is_locked <= 0){
            LOG_MSG_DEBUG(LOG_EN, "Failed to lock SPIFFS");
            ret = ERROR_APP_BUSY;
            break;
        }

        // Check file size first
        size_t file_size = 0;
        if(pal_spiffs_file_get_size(file_path, &file_size) != PAL_OK){
            ret = ERROR_APP_SPIFF_NO_FILE;
            _unlock_spiffs();
            break;
        }

        if(file_size > *read_size){
            LOG_MSG_DEBUG(LOG_EN, "File size is too large: %ld bytes", file_size);
            // Delete oversized file
            pal_spiffs_file_delete(file_path);
            _unlock_spiffs();
            ret = ERROR_APP_SPIFF_NO_FILE;
            break;
        }

        // Read file content using PAL
        size_t bytes_read = 0;
        int32_t pal_ret = pal_spiffs_file_read(file_path, (uint8_t *)content, *read_size, &bytes_read);
        if(pal_ret != PAL_OK){
            ret = ERROR_APP_SPIFF_NO_FILE;
        } else {
            *read_size = bytes_read;
        }

        _unlock_spiffs();

        // LOG_MSG_DEBUG(LOG_EN, "File Content : %s", content);

    }while(false);

    return ret;
}

// bool app_spiffs_raw_open(const char* filepath, void** handler, uint32_t timeout_ms, const char * mode) 
// {
//     LOG_MSG_DEBUG(LOG_EN, "Opening SPIFFS file: %s", filepath);
//     bool result = false;
    
//     do {
//         if (!filepath || !handler) {
//             LOG_MSG_ERROR(LOG_EN, "Invalid parameters");
//             break;
//         }

//         int32_t is_locked = _lock_spiffs(timeout_ms);
//         if (is_locked <= 0) {
//             LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
//             break;
//         }

//         String path = filepath;
//         if (!path.startsWith("/")) {
//             path = "/" + path;
//         }

//         File* file = new File();
//         *file = SPIFFS.open(path, mode);
//         if (!*file) {
//             LOG_MSG_ERROR(LOG_EN, "Failed to open file: %s", filepath);
//             delete file;
//             _unlock_spiffs();
//             break;
//         }

//         *handler = (void*)file;
//         result = true;
//         _unlock_spiffs();

//     } while (false);

//     return result;
// }

// bool app_spiffs_raw_read(void* handler, const char content[], size_t* content_size, uint32_t timeout_ms) {
//     LOG_MSG_DEBUG(LOG_EN, "Reading from SPIFFS file");
//     bool result = false;

//     do {
//         if (!handler || !content_size) {
//             LOG_MSG_ERROR(LOG_EN, "Invalid parameters");
//             break;
//         }

//         int32_t is_locked = _lock_spiffs(timeout_ms);
//         if (is_locked <= 0) {
//             LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
//             break;
//         }

//         File* file = (File*)handler;
//         if (!file->available()) {
//             LOG_MSG_ERROR(LOG_EN, "File not available for reading");
//             _unlock_spiffs();
//             break;
//         }

//         *content_size = file->read((uint8_t*)content, *content_size);
//         result = (*content_size > 0);
//         _unlock_spiffs();

//     } while (false);

//     return result;
// }

// bool app_spiffs_raw_write(void* handler, const char content[], size_t* content_size, uint32_t timeout_ms) 
// {
//     LOG_MSG_DEBUG(LOG_EN, "Writing to SPIFFS file");
//     bool result = false;

//     do {
//         if (!handler || !content || !content_size) {
//             LOG_MSG_ERROR(LOG_EN, "Invalid parameters");
//             break;
//         }

//         int32_t is_locked = _lock_spiffs(timeout_ms);
//         if (is_locked <= 0) {
//             LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
//             break;
//         }

//         File* file = (File*)handler;
//         size_t written = file->write((const uint8_t*)content, *content_size);
//         if (written != *content_size) {
//             LOG_MSG_ERROR(LOG_EN, "Failed to write complete data");
//             _unlock_spiffs();
//             break;
//         }

//         result = true;
//         _unlock_spiffs();

//     } while (false);

//     return result;
// }

// bool pal_spiffs_raw_close(void* handler, uint32_t timeout_ms) 
// {
//     LOG_MSG_DEBUG(LOG_EN, "Closing SPIFFS file");
//     bool result = false;

//     do {
//         if (!handler) {
//             LOG_MSG_ERROR(LOG_EN, "Invalid handler");
//             break;
//         }

//         int32_t is_locked = _lock_spiffs(timeout_ms);
//         if (is_locked <= 0) {
//             LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
//             break;
//         }

//         File* file = (File*)handler;
//         file->close();
//         delete file;
        
//         result = true;
//         _unlock_spiffs();

//     } while (false);

//     return result;
// }

bool app_spiffs_write(file_operation_config_t * config) 
{

    LOG_MSG_DEBUG(LOG_EN, "Writing to SPIFFS file: %s", config->filepath);
    bool result = false;

    do {
        if (!config || !config->filepath || !config->content || !config->content_size) {
            LOG_MSG_ERROR(LOG_EN, "Invalid parameters");
            break;
        }

        LOG_MSG_DEBUG(LOG_EN, "SPIFFS write config: filepath=%s, mode=%s, content_size=%zu", 
                      config->filepath, config->mode, *config->content_size);
    
        LOG_MSG_DEBUG(LOG_EN, "SPIFFS write content: %s", config->content);              

        int32_t is_locked = _lock_spiffs(config->timeout_ms);
        if (is_locked <= 0) {
            LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
            break;
        }

        // Use PAL to write the file
        int32_t pal_ret;
        if(strcmp(config->mode, "a") == 0){
            // Append mode
            pal_ret = pal_spiffs_file_append(config->filepath, (const uint8_t *)config->content, *config->content_size);
        } else {
            // Write mode (default)
            pal_ret = pal_spiffs_file_write(config->filepath, (const uint8_t *)config->content, *config->content_size);
        }
        
        if (pal_ret != PAL_OK) {
            LOG_MSG_ERROR(LOG_EN, "Failed to write file");
            _unlock_spiffs();
            break;
        }

        result = true;
        _unlock_spiffs();

    } while (false);

    return result;
}

bool app_spiffs_read(file_operation_config_t* config) {

    LOG_MSG_DEBUG(LOG_EN, "Reading from SPIFFS file: %s", config->filepath);
    bool result = false;

    do {
        if (!config || !config->filepath || !config->content || !config->content_size) {
            LOG_MSG_ERROR(LOG_EN, "Invalid parameters");
            break;
        }
        
        LOG_MSG_DEBUG(LOG_EN, "SPIFFS read config: filepath=%s, mode=%s, content_size=%zu", 
                      config->filepath, config->mode, *config->content_size);

        int32_t is_locked = _lock_spiffs(config->timeout_ms);
        if (is_locked <= 0) {
            LOG_MSG_ERROR(LOG_EN, "Failed to lock SPIFFS");
            break;
        }

        // Check if file exists
        bool exists = false;
        if(pal_spiffs_file_exists(config->filepath, &exists) != PAL_OK || !exists){
            LOG_MSG_ERROR(LOG_EN, "File does not exist: %s", config->filepath);
            _unlock_spiffs();
            break;
        }

        // NOTE: SPIFFS on ESP-IDF always reports st_size=0 via stat(), so file size
        // checking is skipped — the fread max_size limit acts as the real buffer guard.

        // Read file content
        size_t read_size = 0;
        int32_t pal_ret = pal_spiffs_file_read(config->filepath, (uint8_t *)config->content, *config->content_size, &read_size);
        
        if (pal_ret != PAL_OK) {
            LOG_MSG_ERROR(LOG_EN, "Failed to read file content");
            _unlock_spiffs();
            break;
        }
        
        LOG_MSG_DEBUG(LOG_EN, "SPIFFS read content: %s", config->content);  

        *config->content_size = read_size;
        result = true;
        _unlock_spiffs();

    } while (false);

    return result;
}


