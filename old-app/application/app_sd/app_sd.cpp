
#include <string.h>

#include "app.h"
#include "pal_logger.h"
#include "hsys_mutex.h"
#include "hsys_task.h"
#include "hsys_event.h"

#include "bsp/board.h"
#include "pal/pal_sd.h"

#define __TAG__  "APP_SD  "

static bool _is_initialized = false;
static hsys_mutex_handle_t _sd_mutex_handle;
static fp_app_sd_on_event_t _on_event;

// void _app_sd_process(void * arg);
int32_t _lock_sd(uint32_t timeout_ms);
void _unlock_sd(void);

void app_sd_init(fp_app_sd_on_event_t fp_app_sd_on_event, event_table_t * event_table, int32_t priority){
    
    do{

        if(fp_app_sd_on_event == NULL){
            LOG_MSG_DEBUG(LOG_EN, "app_sd_init: fp_app_sd_on_event is NULL");
            while(1);
        }
        _on_event = fp_app_sd_on_event;

        _sd_mutex_handle = hsys_mutex_create();
        if(_sd_mutex_handle == NULL){
            LOG_MSG_ERROR(LOG_EN, "Critical Error!. app_sd_init: hsys_mutex_create failed");
            while (1);
        }

        // Configure SD card using PAL
        pal_sd_config_t sd_config = {
            .cs_pin = board_get_sd_card_ss_pin(),
            .mosi_pin = board_get_sd_card_mosi_pin(),
            .miso_pin = board_get_sd_card_miso_pin(),
            .sck_pin = board_get_sd_card_sck_pin()
        };

        pal_sd_info_t sd_info = {};
        int32_t ret = pal_sd_init(&sd_config, &sd_info);
        
        if(ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to initialize SD card via PAL");
            break;
        }

        LOG_MSG_DEBUG(LOG_EN, "SD Card Type: %s", sd_info.card_type);
        LOG_MSG_DEBUG(LOG_EN, "SD Card Size: %lluMB", sd_info.card_size_mb);

        _is_initialized = true;

        if(_on_event){
            _on_event(APP_SD_EVNT_SD_READY, &sd_info.card_size_mb);
        }
    }while(false);
}

int32_t _lock_sd(uint32_t timeout_ms){
    if(_sd_mutex_handle){
        return hsys_mutex_try_lock(_sd_mutex_handle, timeout_ms); 
    } 
    return 0;
}

void _unlock_sd(void){
    if(_sd_mutex_handle){
        hsys_mutex_unlock(_sd_mutex_handle);
    }
}

int32_t app_sd_append_line(const char* file_path, const char* line, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_sd(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        // Create line with newline
        char line_with_newline[512];
        snprintf(line_with_newline, sizeof(line_with_newline), "%s\n", line);
        
        // Use PAL to append the line
        int32_t pal_ret = pal_sd_file_append(file_path, line_with_newline, strlen(line_with_newline));
        if(pal_ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to append line to file: %s", file_path);
            ret = ERROR_FAILED;
        }

        _unlock_sd();

    }while(false);
    
    return ret;
}

int32_t app_sd_create_file(const char* file_path, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;

    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_sd(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        // Use PAL to create the file
        int32_t pal_ret = pal_sd_file_create(file_path);
        if(pal_ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to create file: %s", file_path);
            ret = ERROR_FAILED;
        }

        _unlock_sd();

        
    }while(false);
    
    return ret;
}

int32_t app_sd_delete_file(const char* file_path, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_sd(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        // Check if file exists before deleting
        bool exists = false;
        if(pal_sd_file_exists(file_path, &exists) == PAL_OK && exists){
            int32_t pal_ret = pal_sd_file_delete(file_path);
            if(pal_ret != PAL_OK){
                LOG_MSG_ERROR(LOG_EN, "Failed to delete file: %s", file_path);
                ret = ERROR_FAILED;
            }
        }
        
        _unlock_sd();

    }while(false);
    
    return ret;
}

int32_t app_sd_write_file(const char* file_path, const char* content, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{
        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_sd(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }
        
        // Create content with newline
        char content_with_newline[1024];
        snprintf(content_with_newline, sizeof(content_with_newline), "%s\n", content);
        
        // Use PAL to write the file
        int32_t pal_ret = pal_sd_file_write(file_path, content_with_newline, strlen(content_with_newline));
        if(pal_ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to write file: %s", file_path);
            ret = ERROR_FAILED;
        }

        _unlock_sd();

    }while(false);
    
    return ret;
}

int32_t app_sd_read_file(const char* file_path, char* content, size_t max_len, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    
    do{

        if(_is_initialized == false){
            ret = ERROR_APP_NOT_INITIALIZED;
            break;
        }

        int32_t is_locked = _lock_sd(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        // Check file size first
        size_t file_size = 0;
        if(pal_sd_file_get_size(file_path, &file_size) != PAL_OK){
            ret = ERROR_FAILED;
            _unlock_sd();
            break;
        }

        if(file_size > max_len){
            LOG_MSG_DEBUG(LOG_EN, "File size is too large: %ld bytes", file_size);
            // Delete oversized file
            pal_sd_file_delete(file_path);
            _unlock_sd();
            ret = ERROR_FAILED;
            break;
        }

        // Read file content using PAL
        size_t bytes_read = 0;
        int32_t pal_ret = pal_sd_file_read(file_path, content, max_len, &bytes_read);
        if(pal_ret != PAL_OK){
            ret = ERROR_FAILED;
        }

        _unlock_sd();

    }while(false);

    return ret;
}

int32_t app_sd_get_next_file_path_in_logs(char * file_path, size_t max_len, uint32_t * file_idx, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;
    static pal_sd_dir_handle_t root_dir = NULL;
    static char current_dir_path[256] = "/Logs";
    
    do{

        int32_t is_locked = _lock_sd(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        // Reset if index is 0 or invalid
        if((*file_idx) == 0 || (*file_idx) == 0xFFFFFFFF){
            if(root_dir != NULL){
                pal_sd_dir_close(root_dir);
                root_dir = NULL;
            }
            strcpy(current_dir_path, "/Logs");
            if(pal_sd_dir_open(current_dir_path, &root_dir) != PAL_OK){
                LOG_MSG_ERROR(LOG_EN, "Failed to open Logs directory");
                _unlock_sd();
                ret = ERROR_FAILED;
                break;
            }
        }

        if(root_dir == NULL){
            if(pal_sd_dir_open(current_dir_path, &root_dir) != PAL_OK){
                _unlock_sd();
                ret = ERROR_FAILED;
                break;
            }
        }

        pal_sd_dir_entry_t entry = {};
        bool has_more = false;
        
        if(pal_sd_dir_read_next(root_dir, &entry, &has_more) == PAL_OK && has_more){
            if(entry.is_directory){
                // Skip "." and ".." - PAL already handles this
                (*file_idx)++;
                pal_sd_dir_close(root_dir);
                strcpy(current_dir_path, entry.full_path);
                if(pal_sd_dir_open(current_dir_path, &root_dir) == PAL_OK){
                    LOG_MSG_DEBUG(LOG_EN, "New folder found, changing directory");
                } else {
                    root_dir = NULL;
                }
            } else {
                // It's a file
                snprintf(file_path, max_len, "%s", entry.full_path);
                (*file_idx)++;
                LOG_MSG_DEBUG(LOG_EN, "File found: %s", file_path);
            }
        } else {
            // No more files
            LOG_MSG_DEBUG(LOG_EN, "No more files...");
            (*file_idx) = 0xFFFFFFFF;
            if(root_dir != NULL){
                pal_sd_dir_close(root_dir);
                root_dir = NULL;
            }
        }

        _unlock_sd();

    }while(false);

    return ret;
}

int32_t app_sd_remove_dir(const char * path, uint32_t timeout_ms){

    int32_t ret = ERROR_OK;

    do
    {        
        int32_t is_locked = _lock_sd(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        // Use PAL to remove directory
        int32_t pal_ret = pal_sd_dir_remove(path);
        if(pal_ret == PAL_OK){
            LOG_MSG_DEBUG(LOG_EN, "Dir removed, Success");
        } else {
            LOG_MSG_ERROR(LOG_EN, "pal_sd_dir_remove failed for: %s", path);
            ret = ERROR_FAILED;
        }

        _unlock_sd();

    } while (false);
    
    return ret;
}

int32_t app_sd_read_line(const char * path, uint32_t line, char * read_line, size_t max_len, uint32_t timeout_ms){
	
    int32_t ret = ERROR_OK;
    
    do{
        int32_t is_locked = _lock_sd(timeout_ms);
        if(is_locked <= 0){
            ret = ERROR_APP_BUSY;
            break;
        }

        if(read_line == NULL){
            _unlock_sd();
            ret = ERROR_FAILED;
            break;
        }

        // Use PAL to read the specific line
        int32_t pal_ret = pal_sd_file_read_line(path, line, read_line, max_len);
        if(pal_ret != PAL_OK){
            LOG_MSG_ERROR(LOG_EN, "Failed to read line %u from file: %s", line, path);
            ret = ERROR_FAILED;
        }

        _unlock_sd();

    }while(false);

    return ret;
}













