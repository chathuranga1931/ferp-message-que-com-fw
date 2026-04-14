
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

#include "hsys_config.h"

#include "pal_logger.h"

#define __TAG__  "HS_CONF "

#define EXEC_IF_NOT_NULL(x, ...) 		((x != NULL) ? x(__VA_ARGS__) : CONFIG_NULL)
#define EXEC_IF_NOT_NULL_RET(y,x, ...) 		((x != NULL) ? y = x(__VA_ARGS__) : y = CONFIG_NULL)
#define EXEC_IF_NOT_NULL_RET_BREAK(z,y,x, ...) 		\
    if(x){ \
        y = x(__VA_ARGS__); \        
    } else { \
        z = CONFIG_NULL;\
        break;\ 
    }
#define EXEC_IF_NOT_NULL_BREAK(z,x, ...) 		\
    if(x){ \
        x(__VA_ARGS__); \        
    } else { \
        z = CONFIG_NULL;\
        break;\ 
    }

int32_t hsys_config_init(config_init_t config_init, config_handle_t * config_hndl){

    if(config_hndl == nullptr){
        return CONFIG_NULL;
    }

    if(config_init.table == nullptr){
        return CONFIG_NULL;
    }

    if(config_init.config_size == 0){
        return CONFIG_NULL;
    }

    config_hndl->config_size = config_init.config_size;
    config_hndl->table = config_init.table;    

    config_hndl->is_initialized = true;
    return 0;
}

int32_t hsys_config_convert_to_json(config_handle_t * config_hndl, char * json_buffer, size_t buffer_size, size_t * json_length){
    
    int32_t ret = CONFIG_SUCCESS;

    do{
        if(config_hndl == nullptr){
            ret = CONFIG_NULL;
            break;
        }

        if(!config_hndl->is_initialized){
            ret = CONFIG_UNINTIALIZED;  
            break;
        }

        if(json_buffer == nullptr || json_length == nullptr){
            ret = CONFIG_NULL;
            break;
        }

        JsonDocument root;

        // LOG_MSG_DEBUG(std::string(__func__) + ":" + std::to_string(__LINE__));
        for(int i=0; i<config_hndl->config_size; i++){            
            switch (config_hndl->table[i].type){
                case HSYS_TYPE_STRING:
                    root[config_hndl->table[i].name] = (char *)(config_hndl->table[i].p_global_value);
                    // LOG_MSG_DEBUG(LOG_EN, "%s: %s", config_hndl->table[i].name, (char *)(config_hndl->table[i].p_global_value));
                    break;
                case HSYS_TYPE_UINT32: {
                    char uint_str[16];
                    snprintf(uint_str, sizeof(uint_str), "%lu", *((uint32_t *)(config_hndl->table[i].p_global_value)));
                    root[config_hndl->table[i].name] = uint_str;
                    // LOG_MSG_DEBUG(LOG_EN, "%s: %s", config_hndl->table[i].name, uint_str);
                    break;
                }
                case HSYS_TYPE_BOOL:
                    root[config_hndl->table[i].name] = (bool)(*((bool *)(config_hndl->table[i].p_global_value)));
                    // LOG_MSG_DEBUG(LOG_EN, "%s: %d", config_hndl->table[i].name, *((bool *)(config_hndl->table[i].p_global_value)));
                    break;
                default:
                    break;
            }
        }

        // LOG_MSG_DEBUG(std::string(__func__) + ":" + std::to_string(__LINE__));
        size_t len = serializeJson(root, json_buffer, buffer_size);
        *json_length = len;
        
        if(len >= buffer_size){
            ret = CONFIG_BUFFER_TOO_SMALL;
        }
        // LOG_MSG_DEBUG(std::string(__func__) + ":" + std::to_string(__LINE__));

    }while(false);

    return ret; 
}

int32_t hsys_config_load_from_json(config_handle_t * config_hndl, const char * json_string, size_t json_length) {

    int32_t ret = ERROR_OK;

    do{

        if(config_hndl == nullptr){
            ret = CONFIG_NULL;
            break;
        }

        if(!config_hndl->is_initialized){
            ret = CONFIG_UNINTIALIZED;  
            break;
        }

        if(json_string == nullptr){
            ret = CONFIG_NULL;
            break;
        }

		JsonDocument root;
        DeserializationError error = deserializeJson(root, json_string, json_length, DeserializationOption::NestingLimit(20));
        
        if(error) {
            LOG_MSG_ERROR(LOG_EN, "JSON deserialization failed: %s", error.c_str());
            ret = ERROR_OK + 1;  // JSON parse error
            break;
        }

        for(int i=0; i<config_hndl->config_size; i++){
            switch(config_hndl->table[i].type){
                case HSYS_TYPE_UINT32:
					if(root.containsKey(config_hndl->table[i].name)){
                        // Try to get as string first (for compatibility), then as number
                        if(root[config_hndl->table[i].name].is<const char*>()) {
                            const char* str_val = root[config_hndl->table[i].name].as<const char*>();
                            *((uint32_t *)(config_hndl->table[i].p_global_value)) = atoi(str_val);
                        } else {
                            *((uint32_t *)(config_hndl->table[i].p_global_value)) = root[config_hndl->table[i].name].as<uint32_t>();
                        }
                        // LOG_MSG_DEBUG(LOG_EN, "%s : %d", config_hndl->table[i].name, *((uint32_t *)(config_hndl->table[i].p_global_value)));
                    }
                    break;
                case HSYS_TYPE_STRING:
 					if(root.containsKey(config_hndl->table[i].name)){
						const char* str_value = root[config_hndl->table[i].name].as<const char*>();
                        if(str_value != nullptr) {
                            memset((char *)(config_hndl->table[i].p_global_value), 0, config_hndl->table[i].max_length);
                            strncpy((char *)(config_hndl->table[i].p_global_value), str_value, config_hndl->table[i].max_length - 1);
                        }
                        // LOG_MSG_DEBUG(LOG_EN, "%s : %s", config_hndl->table[i].name, (char *)(config_hndl->table[i].p_global_value));
					}
                    break;
                case HSYS_TYPE_BOOL:                    
					if(root.containsKey(config_hndl->table[i].name)){
                        bool value = root[config_hndl->table[i].name].as<bool>();
                        *((bool *)(config_hndl->table[i].p_global_value)) = value;
                        // LOG_MSG_DEBUG(LOG_EN, "%s : %d", config_hndl->table[i].name, *((bool *)(config_hndl->table[i].p_global_value)));
                    }
                    break;
                default:
                    break;
            }
        }
        
	}while(false);

    return ret;
}