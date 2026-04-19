#pragma once

// Pull in standard integer types that ESP-IDF provides via esp_types.h.
// In the simulator build they must come from the system libc headers.
#include <stdint.h>
#include <stdbool.h>

#ifndef ESP_ERR_T_DEFINED
#define ESP_ERR_T_DEFINED
typedef int esp_err_t;
#define ESP_OK    0
#define ESP_FAIL  (-1)
#define ESP_ERR_INVALID_ARG  0x102
#define ESP_ERR_TIMEOUT      0x107
#endif
