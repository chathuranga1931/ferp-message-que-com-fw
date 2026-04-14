#include "hsys_ntp.h"
#include "pal_ntp.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__ "HSYS_NTP"

// Static variables
static bool is_initialized = false;
static hsys_ntp_config_t current_config;

const hsys_ntp_config_t default_config = {
    "pool.ntp.org",    // primary_server
    "time.nist.gov",   // backup_server
    3600,              // sync_interval
    0                  // timezone_offset
};

hsys_ntp_error_t hsys_ntp_init_default(void) {
    return hsys_ntp_init(&default_config);
}

hsys_ntp_error_t hsys_ntp_init(const hsys_ntp_config_t* config) {
    if (config == nullptr) {
        return HSYS_NTP_ERROR_INVALID_PARAMETER;
    }

    if (is_initialized) {
        hsys_ntp_deinit();
    }

    memcpy(&current_config, config, sizeof(hsys_ntp_config_t));
    
    // Configure PAL NTP
    pal_ntp_config_t pal_config = {};
    
    // Set NTP servers
    strncpy(pal_config.servers[0], config->primary_server, PAL_NTP_SERVER_MAX_LEN - 1);
    strncpy(pal_config.servers[1], config->backup_server, PAL_NTP_SERVER_MAX_LEN - 1);
    pal_config.num_servers = 2;
    
    // Set timezone (convert offset to string format)
    snprintf(pal_config.timezone, PAL_NTP_TIMEZONE_MAX_LEN, "UTC%+d", config->timezone_offset / 3600);
    
    // Set sync parameters
    pal_config.sync_interval_ms = config->sync_interval * 1000;
    pal_config.sync_mode = PAL_NTP_SYNC_MODE_IMMED;  // Use immediate mode for faster initial sync
    pal_config.auto_sync = false;  // Don't auto-start, let caller control when to start
    
    int32_t ret = pal_ntp_init(&pal_config, nullptr, nullptr);
    if (ret != 0) {
        LOG_MSG_ERROR(LOG_EN, "Failed to initialize PAL NTP: %d", ret);
        return HSYS_NTP_ERROR_INIT_FAILED;
    }

    is_initialized = true;
    return HSYS_NTP_OK;
}

hsys_ntp_error_t hsys_ntp_sync_start(void) {
    if (!is_initialized) {
        return HSYS_NTP_ERROR_INIT_FAILED;
    }

    int32_t ret = pal_ntp_start();
    if (ret != 0) {
        LOG_MSG_ERROR(LOG_EN, "Failed to start NTP synchronization: %d", ret);
        return HSYS_NTP_ERROR_SYNC_FAILED;
    }

    return HSYS_NTP_OK;
}

hsys_ntp_error_t hsys_ntp_sync_process(void) {
    if (!is_initialized) {
        return HSYS_NTP_ERROR_INIT_FAILED;
    }

    int32_t ret = pal_ntp_timesync_process();
    if (ret < 0) {
        LOG_MSG_ERROR(LOG_EN, "NTP sync failed: %d", ret);
        return HSYS_NTP_ERROR_SYNC_FAILED;
    }
    else if (ret == 0) {
        // Successfully synchronized
        return HSYS_NTP_OK;
    }
    else {
        // Still waiting for sync (ret == 1)
        return HSYS_NTP_WAITING;
    }
}

hsys_ntp_error_t hsys_ntp_get_time(struct tm* time_info) {
    if (!is_initialized || !time_info) {
        return HSYS_NTP_ERROR_INVALID_PARAMETER;
    }

    int32_t ret = pal_ntp_get_time(time_info);
    if (ret != 0) {
        LOG_MSG_ERROR(LOG_EN, "Failed to get NTP time: %d", ret);
        return HSYS_NTP_ERROR_INVALID_PARAMETER;
    }

    return HSYS_NTP_OK;
}

hsys_ntp_error_t hsys_ntp_get_epochtime(time_t * epoch_time) {
    if (!is_initialized || epoch_time == nullptr) {
        return HSYS_NTP_ERROR_INVALID_PARAMETER;
    }

    int32_t ret = pal_ntp_get_epoch_time(epoch_time);
    if (ret != 0) {
        LOG_MSG_ERROR(LOG_EN, "Failed to get epoch time: %d", ret);
        return HSYS_NTP_ERROR_INVALID_PARAMETER;
    }

    return HSYS_NTP_OK;
}

bool hsys_ntp_is_synchronized(void) {
    return pal_ntp_is_synchronized();
}

hsys_ntp_error_t hsys_ntp_format_time(const struct tm* time_info, 
                                     const char* format, 
                                     char* buffer, 
                                     size_t buffer_size) {
    if (!time_info || !format || !buffer || buffer_size == 0) {
        return HSYS_NTP_ERROR_INVALID_PARAMETER;
    }

    if (strftime(buffer, buffer_size, format, time_info) == 0) {
        return HSYS_NTP_ERROR_INVALID_PARAMETER;
    }

    return HSYS_NTP_OK;
}

hsys_ntp_error_t hsys_ntp_deinit(void) {
    if (!is_initialized) {
        return HSYS_NTP_OK;
    }
    
    pal_ntp_stop();
    pal_ntp_deinit();
    
    is_initialized = false;
    return HSYS_NTP_OK;
}

bool hsys_ntp_get_epochtime_wrapper(time_t * epoch_time) {
    return hsys_ntp_get_epochtime(epoch_time) == HSYS_NTP_OK;
}

bool hsys_ntp_init_default_wrapper(void) {
    return hsys_ntp_init_default() == HSYS_NTP_OK;
}

bool hsys_ntp_init_wrapper(const hsys_ntp_config_t * config) {
    return hsys_ntp_init(config) == HSYS_NTP_OK;
}

bool hsys_ntp_sync_start_wrapper(void) {
    return hsys_ntp_sync_start() == HSYS_NTP_OK;
}

bool hsys_ntp_sync_process_wrapper(void) {
    hsys_ntp_error_t ret = hsys_ntp_sync_process();
    if (ret == HSYS_NTP_OK) {
        return true;
    }
    else if (ret == HSYS_NTP_WAITING) {
        return false;
    }
    else {
        LOG_MSG_ERROR(LOG_EN, "NTP sync failed");
        return false;
    }
}

bool hsys_ntp_get_time_wrapper(struct tm * time_info) {
    return hsys_ntp_get_time(time_info) == HSYS_NTP_OK;
}

bool hsys_ntp_is_synchronized_wrapper(void) {
    return hsys_ntp_is_synchronized();
}

bool hsys_ntp_format_time_wrapper(const struct tm * time_info, 
                                  const char * format, 
                                  char * buffer, 
                                  size_t buffer_size) {
    return hsys_ntp_format_time(time_info, format, buffer, buffer_size) == HSYS_NTP_OK;
}

bool hsys_ntp_deinit_wrapper(void) {
    return hsys_ntp_deinit() == HSYS_NTP_OK;
}

