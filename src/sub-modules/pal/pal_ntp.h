/**
 * @file pal_ntp.h
 * @brief Platform Abstraction Layer for NTP (Network Time Protocol) operations
 * 
 * This header provides a platform-independent interface for NTP/SNTP client
 * functionality including time synchronization, timezone management, and
 * time formatting.
 */

#ifndef PAL_NTP_H
#define PAL_NTP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants
// ============================================================================

#define PAL_NTP_MAX_SERVERS         4       /**< Maximum number of NTP servers */
#define PAL_NTP_SERVER_MAX_LEN      64      /**< Maximum length of server name */
#define PAL_NTP_TIMEZONE_MAX_LEN    32      /**< Maximum length of timezone string */

// ============================================================================
// Type Definitions
// ============================================================================

/**
 * @brief NTP synchronization mode
 */
typedef enum {
    PAL_NTP_SYNC_MODE_IMMED = 0,    /**< Update system time immediately when received */
    PAL_NTP_SYNC_MODE_SMOOTH,       /**< Smooth time update (slew) */
} pal_ntp_sync_mode_t;

/**
 * @brief NTP synchronization status
 */
typedef enum {
    PAL_NTP_SYNC_STATUS_RESET = 0,  /**< Time not set */
    PAL_NTP_SYNC_STATUS_COMPLETED,  /**< Time is synchronized */
    PAL_NTP_SYNC_STATUS_IN_PROGRESS,/**< Synchronization in progress */
} pal_ntp_sync_status_t;

/**
 * @brief NTP event types
 */
typedef enum {
    PAL_NTP_EVENT_SYNC_COMPLETED,   /**< Time synchronized successfully */
    PAL_NTP_EVENT_SYNC_FAILED,      /**< Time synchronization failed */
} pal_ntp_event_t;

/**
 * @brief NTP configuration structure
 */
typedef struct {
    char servers[PAL_NTP_MAX_SERVERS][PAL_NTP_SERVER_MAX_LEN];  /**< NTP server addresses */
    uint8_t num_servers;                    /**< Number of configured servers (1-4) */
    
    char timezone[PAL_NTP_TIMEZONE_MAX_LEN]; /**< Timezone string (e.g., "UTC-5" or "EST5EDT") */
    
    uint32_t sync_interval_ms;              /**< Sync interval in milliseconds (0 = default 1 hour) */
    pal_ntp_sync_mode_t sync_mode;          /**< Sync mode (immediate or smooth) */
    
    bool auto_sync;                         /**< Enable automatic periodic sync */
} pal_ntp_config_t;

/**
 * @brief NTP event callback function type
 * 
 * @param event NTP event that occurred
 * @param user_data User data pointer passed during initialization
 */
typedef void (*pal_ntp_event_callback_t)(pal_ntp_event_t event, void* user_data);

// ============================================================================
// NTP Initialization and Configuration Functions
// ============================================================================

/**
 * @brief Get default NTP configuration
 * 
 * Default settings:
 * - Servers: pool.ntp.org, time.nist.gov
 * - Timezone: UTC
 * - Sync interval: 1 hour
 * - Auto sync: enabled
 * 
 * @param config Pointer to configuration structure to fill with defaults
 */
void pal_ntp_get_default_config(pal_ntp_config_t* config);

/**
 * @brief Initialize NTP client with configuration
 * 
 * @param config Pointer to NTP configuration
 * @param event_callback Event callback function (can be NULL)
 * @param user_data User data pointer to pass to callback
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_init(const pal_ntp_config_t* config,
                     pal_ntp_event_callback_t event_callback,
                     void* user_data);

/**
 * @brief Initialize NTP with default configuration
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_init_default(void);

/**
 * @brief Deinitialize NTP client
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_deinit(void);

// ============================================================================
// NTP Synchronization Functions
// ============================================================================

/**
 * @brief Start NTP synchronization
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_start(void);

/**
 * @brief Stop NTP synchronization
 * 
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_stop(void);

int32_t pal_ntp_timesync_process();

/**
 * @brief Get NTP synchronization status
 * 
 * @return Current sync status
 */
pal_ntp_sync_status_t pal_ntp_get_sync_status(void);

/**
 * @brief Check if time is synchronized
 * 
 * @return true if time is synchronized, false otherwise
 */
bool pal_ntp_is_synchronized(void);

// ============================================================================
// Time Retrieval Functions
// ============================================================================

/**
 * @brief Get current time as Unix epoch (seconds since 1970-01-01 00:00:00 UTC)
 * 
 * @param epoch_time Pointer to store epoch time
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_get_epoch_time(time_t* epoch_time);

/**
 * @brief Get current time as struct tm (broken-down time)
 * 
 * @param time_info Pointer to store time information
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_get_time(struct tm* time_info);

/**
 * @brief Get current time in milliseconds since epoch
 * 
 * @param epoch_ms Pointer to store epoch time in milliseconds
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_get_epoch_ms(uint64_t* epoch_ms);

// ============================================================================
// Time Formatting Functions
// ============================================================================

/**
 * @brief Format time into string
 * 
 * Uses strftime format specifiers:
 * - %Y: Year (4 digits)
 * - %m: Month (01-12)
 * - %d: Day (01-31)
 * - %H: Hour (00-23)
 * - %M: Minute (00-59)
 * - %S: Second (00-59)
 * - etc.
 * 
 * @param time_info Time information to format
 * @param format Format string (strftime compatible)
 * @param buffer Buffer to store formatted time
 * @param buffer_size Size of the buffer
 * @return Number of characters written (excluding null terminator), 0 on error
 */
size_t pal_ntp_format_time(const struct tm* time_info,
                           const char* format,
                           char* buffer,
                           size_t buffer_size);

/**
 * @brief Get current time as formatted string
 * 
 * @param format Format string (strftime compatible)
 * @param buffer Buffer to store formatted time
 * @param buffer_size Size of the buffer
 * @return Number of characters written (excluding null terminator), 0 on error
 */
size_t pal_ntp_get_time_string(const char* format,
                               char* buffer,
                               size_t buffer_size);

// ============================================================================
// Timezone Functions
// ============================================================================

/**
 * @brief Set timezone
 * 
 * Timezone format examples:
 * - "UTC" or "UTC0" - UTC time
 * - "EST5EDT,M3.2.0/2,M11.1.0/2" - US Eastern Time with DST
 * - "CST-8" - China Standard Time (UTC+8)
 * - "JST-9" - Japan Standard Time (UTC+9)
 * 
 * @param timezone Timezone string in POSIX format
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_set_timezone(const char* timezone);

/**
 * @brief Get current timezone string
 * 
 * @param buffer Buffer to store timezone string
 * @param buffer_size Size of the buffer
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_get_timezone(char* buffer, size_t buffer_size);

// ============================================================================
// Server Configuration Functions
// ============================================================================

/**
 * @brief Set NTP server (0-3)
 * 
 * @param index Server index (0-3)
 * @param server Server address (hostname or IP)
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_set_server(uint8_t index, const char* server);

/**
 * @brief Get configured NTP server
 * 
 * @param index Server index (0-3)
 * @param buffer Buffer to store server address
 * @param buffer_size Size of the buffer
 * @return 0 on success, negative error code on failure
 */
int32_t pal_ntp_get_server(uint8_t index, char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif // PAL_NTP_H
