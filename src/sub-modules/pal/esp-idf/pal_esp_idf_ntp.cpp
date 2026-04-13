/**
 * @file pal_esp_idf_ntp.cpp
 * @brief ESP-IDF implementation of NTP PAL
 * 
 * This implementation uses ESP-IDF's SNTP (Simple Network Time Protocol)
 * component to provide NTP functionality for ESP32 chips.
 */

#include "pal_ntp.h"
#include "pal_logger.h"

#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#define     __TAG__         "PAL_NTP "

#define NTP_DEBUG_LOG_EN      LOG_DIS

// ============================================================================
// Static Variables
// ============================================================================

static bool s_ntp_initialized = false;
static bool s_ntp_started = false;
static pal_ntp_sync_status_t s_sync_status = PAL_NTP_SYNC_STATUS_RESET;
static pal_ntp_event_callback_t s_event_callback = NULL;
static void* s_user_data = NULL;
static char s_current_timezone[PAL_NTP_TIMEZONE_MAX_LEN] = "UTC";

// Store server names statically so pointers remain valid after pal_ntp_init() returns
static char s_ntp_servers[PAL_NTP_MAX_SERVERS][PAL_NTP_SERVER_MAX_LEN];

// Event group for sync completion
static EventGroupHandle_t s_sntp_event_group = NULL;
#define SNTP_SYNC_DONE_BIT BIT0

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief SNTP time sync notification callback
 */
static void sntp_sync_time_cb(struct timeval *tv) {
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "Time synchronized: %s", strftime_buf);
    s_sync_status = PAL_NTP_SYNC_STATUS_COMPLETED;
    
    if (s_sntp_event_group) {
        xEventGroupSetBits(s_sntp_event_group, SNTP_SYNC_DONE_BIT);
    }
    
    if (s_event_callback) {
        s_event_callback(PAL_NTP_EVENT_SYNC_COMPLETED, s_user_data);
    }
}

// ============================================================================
// NTP Initialization and Configuration Functions
// ============================================================================

void pal_ntp_get_default_config(pal_ntp_config_t* config) {
    if (config == NULL) {
        return;
    }
    
    memset(config, 0, sizeof(pal_ntp_config_t));
    
    // Default NTP servers - same as ESP-IDF example defaults
    strncpy(config->servers[0], "pool.ntp.org", PAL_NTP_SERVER_MAX_LEN - 1);
    strncpy(config->servers[1], "time.nist.gov", PAL_NTP_SERVER_MAX_LEN - 1);
    strncpy(config->servers[2], "time.google.com", PAL_NTP_SERVER_MAX_LEN - 1);
    config->num_servers = 3;
    
    // Default timezone (UTC)
    strncpy(config->timezone, "UTC", PAL_NTP_TIMEZONE_MAX_LEN - 1);
    
    // Default sync interval (1 hour)
    config->sync_interval_ms = 3600000;
    
    // Default sync mode (immediate)
    config->sync_mode = PAL_NTP_SYNC_MODE_IMMED;
    
    // Auto sync disabled - caller controls when to start
    config->auto_sync = false;
}

int32_t pal_ntp_init(const pal_ntp_config_t* config,
                     pal_ntp_event_callback_t event_callback,
                     void* user_data) {
    
    if (config == NULL) {
        LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "Config is NULL");
        return -1;
    }
    
    if (s_ntp_initialized) {
        LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "NTP already initialized, deinitializing first");
        pal_ntp_deinit();
    }
    
    // Store callback and user data
    s_event_callback = event_callback;
    s_user_data = user_data;
    
    // Create event group for sync
    if (s_sntp_event_group == NULL) {
        s_sntp_event_group = xEventGroupCreate();
        if (s_sntp_event_group == NULL) {
            LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "Failed to create event group");
            return -1;
        }
    }
    
    // Set timezone
    if (strlen(config->timezone) > 0) {
        setenv("TZ", config->timezone, 1);
        tzset();
        strncpy(s_current_timezone, config->timezone, PAL_NTP_TIMEZONE_MAX_LEN - 1);
        LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "Timezone set to: %s", config->timezone);
    }
    
    // Copy server names into static storage so pointers stay valid after this function returns
    memset(s_ntp_servers, 0, sizeof(s_ntp_servers));
    for (uint8_t i = 0; i < config->num_servers && i < PAL_NTP_MAX_SERVERS; i++) {
        if (strlen(config->servers[i]) > 0) {
            strncpy(s_ntp_servers[i], config->servers[i], PAL_NTP_SERVER_MAX_LEN - 1);
            LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "NTP server %d: %s", i, s_ntp_servers[i]);
        }
    }
    
    // Configure SNTP using the new ESP-IDF v5.x API
    // IMPORTANT: Use static server string pointer - must remain valid for SNTP lifetime
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_ntp_servers[0]);
    
    // Set sync callback
    sntp_config.sync_cb = sntp_sync_time_cb;
    
    // Set sync mode
    sntp_config.smooth_sync = (config->sync_mode == PAL_NTP_SYNC_MODE_SMOOTH);
    
    // Don't auto-start, we'll start it explicitly
    sntp_config.start = false;
    
    // Initialize SNTP with the new API
    esp_err_t err = esp_netif_sntp_init(&sntp_config);
    if (err != ESP_OK) {
        LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "Failed to initialize SNTP: %d", err);
        return -1;
    }
    
    // Add additional servers using static server strings
    for (uint8_t i = 1; i < config->num_servers && i < PAL_NTP_MAX_SERVERS; i++) {
        if (strlen(s_ntp_servers[i]) > 0) {
            esp_sntp_setservername(i, s_ntp_servers[i]);
            LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "Added extra NTP server %d: %s", i, s_ntp_servers[i]);
        }
    }
    
    s_ntp_initialized = true;
    s_sync_status = PAL_NTP_SYNC_STATUS_RESET;
    
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "NTP initialized successfully");
    
    // Auto-start if enabled
    if (config->auto_sync) {
        return pal_ntp_start();
    }
    
    return 0;
}

int32_t pal_ntp_init_default(void) {
    pal_ntp_config_t config;
    pal_ntp_get_default_config(&config);
    return pal_ntp_init(&config, NULL, NULL);
}

int32_t pal_ntp_deinit(void) {
    if (s_ntp_started) {
        pal_ntp_stop();
    }
    
    // Deinitialize SNTP using the new API
    if (s_ntp_initialized) {
        esp_netif_sntp_deinit();
    }
    
    if (s_sntp_event_group) {
        vEventGroupDelete(s_sntp_event_group);
        s_sntp_event_group = NULL;
    }
    
    s_ntp_initialized = false;
    s_sync_status = PAL_NTP_SYNC_STATUS_RESET;
    s_event_callback = NULL;
    s_user_data = NULL;
    
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "NTP deinitialized");
    return 0;
}

// ============================================================================
// NTP Synchronization Functions
// ============================================================================

int32_t pal_ntp_start(void) {
    if (!s_ntp_initialized) {
        LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "NTP not initialized");
        return -1;
    }
    
    if (s_ntp_started) {
        LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "NTP already started");
        return 0;
    }
    
    // ESP-IDF SNTP requires system time to be initialized to a reasonable value
    // before it will start syncing. Set it to 2020-01-01 if not already set.
    time_t now;
    time(&now);
    
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "Current system time before NTP start: %ld", now);
    
    // Check if time is invalid (before 2020 or negative)
    if (now < 1577836800 || now < 0) { 
        LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "System time is invalid (%ld), forcing to 2020-01-01", now);
        struct timeval tv = {
            .tv_sec = 1577836800,  // 2020-01-01 00:00:00 UTC
            .tv_usec = 0
        };
        int ret = settimeofday(&tv, NULL);
        LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "settimeofday returned: %d", ret);
        
        // Verify it was set
        time(&now);
        LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "System time after settimeofday: %ld", now);
        
        if (now < 1577836800) {
            LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "CRITICAL: Failed to set system time! Still: %ld", now);
            // Try alternative method
            struct timespec ts = {
                .tv_sec = 1577836800,
                .tv_nsec = 0
            };
            clock_settime(CLOCK_REALTIME, &ts);
            time(&now);
            LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "After clock_settime: %ld", now);
        }
    }
    
    // Start SNTP using the new ESP-IDF v5.x API
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "Starting SNTP service...");
    esp_err_t err = esp_netif_sntp_start();
    if (err != ESP_OK) {
        LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "Failed to start SNTP: %d", err);
        return -1;
    }
    
    s_ntp_started = true;
    s_sync_status = PAL_NTP_SYNC_STATUS_IN_PROGRESS;
    
    // Check SNTP status immediately after start
    sntp_sync_status_t sntp_status = esp_sntp_get_sync_status();
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "NTP started successfully, initial SNTP status: %d", sntp_status);
    
    // Log sync mode
    uint8_t sync_mode = esp_sntp_get_sync_mode();
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "SNTP sync mode: %d (0=IMMED, 1=SMOOTH)", sync_mode);
    
    return 0;
}

int32_t pal_ntp_stop(void) {
    if (!s_ntp_started) {
        return 0;
    }
    
    // In ESP-IDF v5.x, there's no separate stop - we keep it running
    // or deinit completely. Just mark as stopped.
    s_ntp_started = false;
    
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "NTP stopped");
    return 0;
}

int32_t pal_ntp_timesync_process() {

    if (!s_ntp_initialized) {
        LOG_MSG_ERROR(NTP_DEBUG_LOG_EN, "NTP not initialized");
        return -1;
    }
    
    // Start NTP if not already started
    if (!s_ntp_started) {

        LOG_MSG_DEBUG(NTP_DEBUG_LOG_EN, "NTP is not started, starting synchronization...");

        int32_t ret = pal_ntp_start();
        if (ret != 0) {
            return ret;
        }

        // Clear sync bit
        if (s_sntp_event_group) {
            xEventGroupClearBits(s_sntp_event_group, SNTP_SYNC_DONE_BIT);
        }
    }    
    
    // Check actual SNTP sync status from ESP-IDF
    sntp_sync_status_t sntp_status = esp_sntp_get_sync_status();
    
    if (sntp_status == SNTP_SYNC_STATUS_COMPLETED) {
        LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "Time synchronized successfully (via status check)");
        s_sync_status = PAL_NTP_SYNC_STATUS_COMPLETED;
        
        // Set the event bit in case callback didn't fire
        if (s_sntp_event_group) {
            xEventGroupSetBits(s_sntp_event_group, SNTP_SYNC_DONE_BIT);
        }
        
        return 0;
    }
    
    // Also check via event group (in case callback fired)
    if (s_sntp_event_group) {
        EventBits_t bits = xEventGroupWaitBits(
            s_sntp_event_group,
            SNTP_SYNC_DONE_BIT,
            pdFALSE,
            pdFALSE,
            10 / portTICK_PERIOD_MS
        );
        
        if (bits & SNTP_SYNC_DONE_BIT) 
        {
            LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "Time synchronized successfully (via event)");
            return 0;
        }
    }
    
    // Still waiting
    LOG_MSG_DEBUG(NTP_DEBUG_LOG_EN, "NTP sync in progress (status: %d)", sntp_status);
    return 1;
}

pal_ntp_sync_status_t pal_ntp_get_sync_status(void) {
    sntp_sync_status_t status = sntp_get_sync_status();
    
    switch (status) {
        case SNTP_SYNC_STATUS_RESET:
            return PAL_NTP_SYNC_STATUS_RESET;
        case SNTP_SYNC_STATUS_COMPLETED:
            return PAL_NTP_SYNC_STATUS_COMPLETED;
        case SNTP_SYNC_STATUS_IN_PROGRESS:
            return PAL_NTP_SYNC_STATUS_IN_PROGRESS;
        default:
            return PAL_NTP_SYNC_STATUS_RESET;
    }
}

bool pal_ntp_is_synchronized(void) {
    return (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
}

// ============================================================================
// Time Retrieval Functions
// ============================================================================

int32_t pal_ntp_get_epoch_time(time_t* epoch_time) {
    if (epoch_time == NULL) {
        return -1;
    }
    
    time(epoch_time);
    
    // Check if time is set (year > 2020)
    if (*epoch_time < 1577836800) { // 2020-01-01 00:00:00 UTC
        return -1;
    }
    
    return 0;
}

int32_t pal_ntp_get_time(struct tm* time_info) {
    if (time_info == NULL) {
        return -1;
    }
    
    time_t now;
    if (pal_ntp_get_epoch_time(&now) != 0) {
        return -1;
    }
    
    localtime_r(&now, time_info);
    return 0;
}

int32_t pal_ntp_get_epoch_ms(uint64_t* epoch_ms) {
    if (epoch_ms == NULL) {
        return -1;
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    *epoch_ms = ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
    
    // Check if time is set
    if (tv.tv_sec < 1577836800) { // 2020-01-01 00:00:00 UTC
        return -1;
    }
    
    return 0;
}

// ============================================================================
// Time Formatting Functions
// ============================================================================

size_t pal_ntp_format_time(const struct tm* time_info,
                           const char* format,
                           char* buffer,
                           size_t buffer_size) {
    if (time_info == NULL || format == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    
    return strftime(buffer, buffer_size, format, time_info);
}

size_t pal_ntp_get_time_string(const char* format,
                               char* buffer,
                               size_t buffer_size) {
    if (format == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    
    struct tm time_info;
    if (pal_ntp_get_time(&time_info) != 0) {
        return 0;
    }
    
    return strftime(buffer, buffer_size, format, &time_info);
}

// ============================================================================
// Timezone Functions
// ============================================================================

int32_t pal_ntp_set_timezone(const char* timezone) {
    if (timezone == NULL) {
        return -1;
    }
    
    setenv("TZ", timezone, 1);
    tzset();
    
    strncpy(s_current_timezone, timezone, PAL_NTP_TIMEZONE_MAX_LEN - 1);
    s_current_timezone[PAL_NTP_TIMEZONE_MAX_LEN - 1] = '\0';
    
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "Timezone set to: %s", timezone);
    return 0;
}

int32_t pal_ntp_get_timezone(char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    strncpy(buffer, s_current_timezone, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    
    return 0;
}

// ============================================================================
// Server Configuration Functions
// ============================================================================

int32_t pal_ntp_set_server(uint8_t index, const char* server) {
    if (server == NULL || index >= PAL_NTP_MAX_SERVERS) {
        return -1;
    }
    
    esp_sntp_setservername(index, server);
    LOG_MSG_INFO(NTP_DEBUG_LOG_EN, "NTP server %d set to: %s", index, server);
    
    return 0;
}

int32_t pal_ntp_get_server(uint8_t index, char* buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0 || index >= PAL_NTP_MAX_SERVERS) {
        return -1;
    }
    
    const char* server = esp_sntp_getservername(index);
    if (server == NULL) {
        return -1;
    }
    
    strncpy(buffer, server, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    
    return 0;
}
