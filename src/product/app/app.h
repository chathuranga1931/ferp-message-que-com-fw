#pragma once

#include "hsys_config.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

void app_init(void);
void app_run(void);

/** Returns a pointer to the live config handle (owned by app.cpp). */
config_handle_t *app_config_get_handle(void);

/** Returns the config field table and optionally its size. */
config_t *app_config_get_table(uint16_t *out_size);

#ifdef __cplusplus
}
#endif
