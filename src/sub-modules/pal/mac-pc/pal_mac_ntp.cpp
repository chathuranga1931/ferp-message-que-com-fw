/**
 * @file pal_mac_ntp.cpp
 * @brief PAL NTP stub for the macOS simulator.
 *
 * The Mac host is assumed to be already time-synchronised via the OS NTP
 * daemon.  All time queries therefore delegate to gettimeofday() /
 * localtime_r() rather than running an actual SNTP client.
 *
 * Behaviour:
 *   - pal_ntp_start() immediately marks sync as COMPLETED and fires the
 *     callback (if any).  ModuleTimeMgr can call pal_ntp_timesync_process()
 *     any time afterward and receive 0 (done).
 *   - All time getters use the host clock, so the simulated firmware sees
 *     real wall-clock time.
 */

#include "pal_ntp.h"
#include "pal_logger.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>   /* setenv / tzset */

#define __TAG__      "PAL_NTP "
#define NTP_LOG      false

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

static bool                     s_initialized   = false;
static bool                     s_started        = false;
static pal_ntp_sync_status_t    s_sync_status   = PAL_NTP_SYNC_STATUS_RESET;
static pal_ntp_event_callback_t s_callback      = NULL;
static void                    *s_user_data     = NULL;
static char s_timezone[PAL_NTP_TIMEZONE_MAX_LEN] = "UTC";

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void pal_ntp_get_default_config(pal_ntp_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    strncpy(config->servers[0], "pool.ntp.org",    PAL_NTP_SERVER_MAX_LEN - 1);
    strncpy(config->servers[1], "time.nist.gov",   PAL_NTP_SERVER_MAX_LEN - 1);
    strncpy(config->servers[2], "time.google.com", PAL_NTP_SERVER_MAX_LEN - 1);
    config->num_servers      = 3;
    strncpy(config->timezone, "UTC", PAL_NTP_TIMEZONE_MAX_LEN - 1);
    config->sync_interval_ms = 3600000u;
    config->sync_mode        = PAL_NTP_SYNC_MODE_IMMED;
    config->auto_sync        = false;
}

int32_t pal_ntp_init(const pal_ntp_config_t *config,
                     pal_ntp_event_callback_t event_callback,
                     void *user_data)
{
    if (!config) return -1;

    s_callback   = event_callback;
    s_user_data  = user_data;
    s_sync_status = PAL_NTP_SYNC_STATUS_RESET;
    s_started    = false;
    s_initialized = true;

    if (strlen(config->timezone) > 0u) {
        strncpy(s_timezone, config->timezone, PAL_NTP_TIMEZONE_MAX_LEN - 1);
        s_timezone[PAL_NTP_TIMEZONE_MAX_LEN - 1] = '\0';
        setenv("TZ", s_timezone, 1);
        tzset();
    }

    LOG_MSG_INFO(NTP_LOG, "NTP initialized (mac stub)");

    if (config->auto_sync) {
        return pal_ntp_start();
    }
    return 0;
}

int32_t pal_ntp_init_default(void)
{
    pal_ntp_config_t cfg;
    pal_ntp_get_default_config(&cfg);
    return pal_ntp_init(&cfg, NULL, NULL);
}

int32_t pal_ntp_deinit(void)
{
    s_initialized = false;
    s_started     = false;
    s_sync_status = PAL_NTP_SYNC_STATUS_RESET;
    s_callback    = NULL;
    s_user_data   = NULL;
    LOG_MSG_INFO(NTP_LOG, "NTP deinitialized");
    return 0;
}

// ---------------------------------------------------------------------------
// Synchronization control
// ---------------------------------------------------------------------------

int32_t pal_ntp_start(void)
{
    if (!s_initialized) return -1;
    if (s_started) return 0;

    s_started     = true;
    s_sync_status = PAL_NTP_SYNC_STATUS_COMPLETED;

    LOG_MSG_INFO(NTP_LOG, "NTP start: host clock used, marking COMPLETED");

    if (s_callback) {
        s_callback(PAL_NTP_EVENT_SYNC_COMPLETED, s_user_data);
    }
    return 0;
}

int32_t pal_ntp_stop(void)
{
    s_started = false;
    return 0;
}

int32_t pal_ntp_timesync_process(void)
{
    if (!s_initialized) {
        /* Auto-init on first call for convenience */
        pal_ntp_init_default();
    }
    if (!s_started) {
        pal_ntp_start();
    }
    /* Sync is always immediately complete on the mac stub */
    return (s_sync_status == PAL_NTP_SYNC_STATUS_COMPLETED) ? 0 : 1;
}

pal_ntp_sync_status_t pal_ntp_get_sync_status(void)
{
    return s_sync_status;
}

bool pal_ntp_is_synchronized(void)
{
    return (s_sync_status == PAL_NTP_SYNC_STATUS_COMPLETED);
}

// ---------------------------------------------------------------------------
// Time retrieval
// ---------------------------------------------------------------------------

int32_t pal_ntp_get_epoch_time(time_t *epoch_time)
{
    if (!epoch_time) return -1;
    time(epoch_time);
    return (*epoch_time >= 1577836800LL) ? 0 : -1;  /* valid if ≥ 2020-01-01 */
}

int32_t pal_ntp_get_time(struct tm *time_info)
{
    if (!time_info) return -1;
    time_t now;
    if (pal_ntp_get_epoch_time(&now) != 0) return -1;
    localtime_r(&now, time_info);
    return 0;
}

int32_t pal_ntp_get_epoch_ms(uint64_t *epoch_ms)
{
    if (!epoch_ms) return -1;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec < 1577836800LL) return -1;
    *epoch_ms = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
    return 0;
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------

size_t pal_ntp_format_time(const struct tm *time_info, const char *format,
                            char *buffer, size_t buffer_size)
{
    if (!time_info || !format || !buffer || buffer_size == 0u) return 0u;
    return strftime(buffer, buffer_size, format, time_info);
}

size_t pal_ntp_get_time_string(const char *format, char *buffer, size_t buffer_size)
{
    if (!format || !buffer || buffer_size == 0u) return 0u;
    struct tm tm_info;
    if (pal_ntp_get_time(&tm_info) != 0) return 0u;
    return strftime(buffer, buffer_size, format, &tm_info);
}

// ---------------------------------------------------------------------------
// Timezone
// ---------------------------------------------------------------------------

int32_t pal_ntp_set_timezone(const char *timezone)
{
    if (!timezone) return -1;
    strncpy(s_timezone, timezone, PAL_NTP_TIMEZONE_MAX_LEN - 1);
    s_timezone[PAL_NTP_TIMEZONE_MAX_LEN - 1] = '\0';
    setenv("TZ", s_timezone, 1);
    tzset();
    return 0;
}

int32_t pal_ntp_get_timezone(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0u) return -1;
    strncpy(buffer, s_timezone, buffer_size - 1u);
    buffer[buffer_size - 1u] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// Server config  (no-ops on the mac stub)
// ---------------------------------------------------------------------------

int32_t pal_ntp_set_server(uint8_t index, const char *server)
{
    (void)index; (void)server;
    return 0;
}

int32_t pal_ntp_get_server(uint8_t index, char *buffer, size_t buffer_size)
{
    (void)index;
    if (!buffer || buffer_size == 0u) return -1;
    buffer[0] = '\0';
    return 0;
}
