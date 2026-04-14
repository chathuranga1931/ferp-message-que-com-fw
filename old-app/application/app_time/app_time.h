/**
 * @file app_time.h
 * @brief Application time manager interface
 */

#ifndef APP_TIME_H
#define APP_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "app_common.h"
#include "hsys_ntp.h"
#include "hsys_rtc.h"
#include "app.h"

#include "user_config.h"


typedef enum{
    APP_TIMEMAN_READY,
    APP_TIMEMAN_TIME_UPDATED_FROM_RTC,
    APP_TIMEMAN_TIME_UPDATED_FROM_NTP,
    APP_TIMEMAN_TIME_UPDATED_FROM_BACKUP_FILE,

    APP_TIMEMAN_TIME_UPDATE_FAILED_FROM_RTC,
    APP_TIMEMAN_TIME_UPDATE_FAILED_FROM_NTP,
    APP_TIMEMAN_TIME_UPDATE_FAILED_CRITICAL,

     /* Add more events as needed */
}app_timeman_event_t;

typedef void (*fp_app_timeman_on_event_t)(app_timeman_event_t event, void * arg);


typedef struct {
    const char * local_backup_filepath;
    const hsys_spiffs_t * spiffs;
    const hsys_rtc_t * rtc;
    const hsys_ntp_t * ntp;
} timeman_configs_t;

typedef struct {
    const timeman_configs_t * p_configs;
    fp_app_timeman_on_event_t fp_app_timeman_on_event;
    app_init_t app_init;
} timeman_init_t;


/**
 * @brief Initialize time manager
 * 
 * @param p_timeman_init Pointer to initialization structure
 * @return int32_t 0 on success, error code otherwise
 */
int32_t app_timeman_init(const timeman_init_t * p_timeman_init);

/**
 * @brief Get last working time
 * 
 * @param time Pointer to store the time
 * @return int32_t 0 on success, error code otherwise
 */
int32_t timeman_get_last_working(time_t * time);

/**
 * @brief Update last working time
 * 
 * @return int32_t 0 on success, error code otherwise
 */
int32_t timeman_update_last_working_time(void);

/**
 * @brief Run time manager task
 */
void app_timeman_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TIME_H */
