
#ifndef MIDDLEWARE_HSYS_CONFIG_H
#define MIDDLEWARE_HSYS_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hsys_type.h"

#include "user_config.h"

#ifndef ERR_CONFIG_OFFSET
#error "Define ERR_CONFIG_OFFSET"
#endif

#ifndef ERROR_OK
#error "Define ERROR_OK"
#endif

#define CONFIG_SUCCESS 		    (ERROR_OK)
#define CONFIG_UNINTIALIZED	    (ERR_CONFIG_OFFSET + 0)
#define CONFIG_NULL       	    (ERR_CONFIG_OFFSET + 1)
#define CONFIG_BUFFER_TOO_SMALL  (ERR_CONFIG_OFFSET + 2)

typedef struct{
	uint16_t key;           ///< Unique 16-bit identifier for this config field (CFG_KEY_*)
	char name[32];
	hsys_type_t type;
	void * p_global_value;
	uint32_t max_length;
}config_t;

typedef struct{
    uint16_t config_size;       //no of configurations available
    config_t * table;
}config_init_t;

typedef struct {
	bool is_initialized;
    uint16_t config_size;       //no of configurations available
    config_t * table;
}config_handle_t;

int32_t hsys_config_init(config_init_t config_init, config_handle_t * config_hndl);
int32_t hsys_config_convert_to_json(config_handle_t * config_hndl, char * json_buffer, size_t buffer_size, size_t * json_length);
int32_t hsys_config_load_from_json(config_handle_t * config_hndl, const char * json_string, size_t json_length);

#endif //MIDDLEWARE_HSYS_CONFIG_H
