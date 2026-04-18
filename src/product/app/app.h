#pragma once

#include "hsys_config.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_init(void);
void app_run(void);

/** Loads config defaults and initialises the config handle.
 *  Call this before the module/task lifecycle when not using app_init(). */
void app_config_init(void);

/** Returns a pointer to the live config handle (owned by app.cpp). */
config_handle_t *app_config_get_handle(void);

/** Returns the config field table and optionally its size. */
config_t *app_config_get_table(uint16_t *out_size);

#ifdef __cplusplus
}
#endif
