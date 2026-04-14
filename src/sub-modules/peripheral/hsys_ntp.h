/**
 * @file hsys_ntp.h
 * @brief NTP client functionality for ESP32
 */

#ifndef HSYS_NTP_H
#define HSYS_NTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>
#include <stdbool.h>

/**
 * @brief Maximum length for NTP server address
 */
#define HSYS_NTP_SERVER_MAX_LENGTH 64

/**
 * @brief Error codes for NTP operations
 */
typedef enum {
    HSYS_NTP_OK = 0,
    HSYS_NTP_WAITING,
    HSYS_NTP_ERROR_INIT_FAILED,
    HSYS_NTP_ERROR_SYNC_FAILED,
    HSYS_NTP_ERROR_INVALID_PARAMETER
} hsys_ntp_error_t;

/**
 * @brief NTP client configuration
 */
typedef struct {
    char primary_server[HSYS_NTP_SERVER_MAX_LENGTH];   /**< Primary NTP server address */
    char backup_server[HSYS_NTP_SERVER_MAX_LENGTH];    /**< Backup NTP server address */
    int sync_interval;                                 /**< Time sync interval in seconds */
    int timezone_offset;                               /**< Timezone offset in seconds */
} hsys_ntp_config_t;

/**
 * @brief Initialize NTP with default settings
 * 
 * Default primary server: "pool.ntp.org"
 * Default backup server: "time.nist.gov"
 * Default sync interval: 3600 seconds (1 hour)
 * 
 * @return HSYS_NTP_OK on success, error code otherwise
 */
hsys_ntp_error_t hsys_ntp_init_default(void);

/**
 * @brief Initialize NTP with custom configuration
 * 
 * @param config Pointer to configuration structure
 * @return HSYS_NTP_OK on success, error code otherwise
 */
hsys_ntp_error_t hsys_ntp_init(const hsys_ntp_config_t* config);

/**
 * @brief Force synchronization with NTP server
 * 
 * @return HSYS_NTP_OK on success, error code otherwise
 */
hsys_ntp_error_t hsys_ntp_sync_now(void);

/**
 * @brief Get current synchronized time
 * 
 * @param time_info Pointer to store the time information
 * @return HSYS_NTP_OK on success, error code otherwise
 */
hsys_ntp_error_t hsys_ntp_get_time(struct tm* time_info);

/**
 * @brief Check if time is synchronized
 * 
 * @return true if time is synchronized, false otherwise
 */
bool hsys_ntp_is_synchronized(void);

/**
 * @brief Format time into string
 * 
 * @param time_info Time information to format
 * @param format Format string (strftime compatible)
 * @param buffer Buffer to store formatted time
 * @param buffer_size Size of the buffer
 * @return HSYS_NTP_OK on success, error code otherwise
 */
hsys_ntp_error_t hsys_ntp_format_time(const struct tm* time_info, 
                                      const char* format, 
                                      char* buffer, 
                                      size_t buffer_size);
            


hsys_ntp_error_t hsys_ntp_get_epochtime(time_t * epoch_time);
                                      
/**
 * @brief De-initialize NTP client
 * 
 * @return HSYS_NTP_OK on success, error code otherwise
 */
hsys_ntp_error_t hsys_ntp_deinit(void);

bool hsys_ntp_init_default_wrapper(void);
bool hsys_ntp_init_wrapper(const hsys_ntp_config_t * config);
bool hsys_ntp_sync_start_wrapper(void) ;
bool hsys_ntp_sync_process_wrapper(void) ;
bool hsys_ntp_get_time_wrapper(struct tm * time_info);
bool hsys_ntp_is_synchronized_wrapper(void) ;
bool hsys_ntp_format_time_wrapper(const struct tm * time_info, 
                                  const char * format, 
                                  char * buffer, 
                                  size_t buffer_size);
bool hsys_ntp_deinit_wrapper(void);
bool hsys_ntp_get_epochtime_wrapper(time_t * epoch_time);

typedef struct{
    
    bool (*fp_hsys_ntp_init_default)(void);
    bool (*fp_hsys_ntp_init)(const hsys_ntp_config_t * config);
    bool (*fp_hsys_ntp_sync_start)(void);
    bool (*fp_hsys_ntp_sync_process)(void);
    bool (*fp_hsys_ntp_get_time)(struct tm * time_info);
    bool (*fp_hsys_ntp_is_synchronized)(void);
    bool (*fp_hsys_ntp_format_time)(const struct tm * time_info, 
                                                const char * format, 
                                                char * buffer, 
                                                size_t buffer_size);
    bool (*fp_hsys_ntp_deinit)(void);
    bool (*fp_hsys_ntp_get_epochtime)(time_t * epoch_time);
}hsys_ntp_t;



const static hsys_ntp_t ntp_default = {
    .fp_hsys_ntp_init_default = hsys_ntp_init_default_wrapper,
    .fp_hsys_ntp_init = hsys_ntp_init_wrapper,
    .fp_hsys_ntp_sync_start = hsys_ntp_sync_start_wrapper,
    .fp_hsys_ntp_sync_process = hsys_ntp_sync_process_wrapper,
    .fp_hsys_ntp_get_time = hsys_ntp_get_time_wrapper,
    .fp_hsys_ntp_is_synchronized = hsys_ntp_is_synchronized_wrapper,
    .fp_hsys_ntp_format_time = hsys_ntp_format_time_wrapper,
    .fp_hsys_ntp_deinit = hsys_ntp_deinit_wrapper,
    .fp_hsys_ntp_get_epochtime = hsys_ntp_get_epochtime_wrapper
};

#ifdef __cplusplus
}
#endif

#endif /* HSYS_NTP_H */
