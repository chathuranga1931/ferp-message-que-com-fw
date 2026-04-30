#ifndef APP_OTA_H
#define APP_OTA_H

#include <stdint.h>

#include "app_common.h"
#include "hsys_ota.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Init structure
 *
 * Pass an array of fully-configured hsys_ota_driver_t pointers.
 * app_ota will run each driver independently on every timer tick —
 * each has its own server URL, device ID, firmware type, and flash writer.
 *
 * Example (two drivers):
 * @code
 *   hsys_ota_driver_t* drivers[] = { &_esp32main_ota_driver, &_esp07dt_ota_driver };
 *   const app_ota_init_t init = {
 *       .drivers      = drivers,
 *       .driver_count = 2,
 *       .app_init     = { ... },
 *   };
 * @endcode
 * ========================================================================= */

typedef enum {
    APP_OTA_EVENT_NONE = 0,
    APP_OTA_EVENT_CHECK_STARTED,
    APP_OTA_EVENT_CHECK_SUCCESS,
    APP_OTA_EVENT_CHECK_FAILURE,
    APP_OTA_EVENT_DOWNLOAD_STARTED,
    APP_OTA_EVENT_DOWNLOAD_SUCCESS,
    APP_OTA_EVENT_DOWNLOAD_FAILURE,
     /* Add more events as needed */
} app_ota_event_t;

typedef void (*fp_app_ota_on_event_t)(app_ota_event_t event, void * arg);

typedef struct {
    fp_app_ota_on_event_t fp_app_ota_on_event;
    hsys_ota_driver_t** drivers;       /**< Array of pointers to configured OTA drivers */
    uint8_t             driver_count;  /**< Number of entries in drivers[] */
    app_init_t          app_init;
} app_ota_init_t;

/* =========================================================================
 * API
 * ========================================================================= */

int32_t app_ota_init(const app_ota_init_t* init);
int32_t app_ota_run(void);
int32_t app_ota_trigger_check(void);
int32_t app_ota_on_driver_ready(uint8_t driver_index);
int32_t app_ota_device_network_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_OTA_H */
