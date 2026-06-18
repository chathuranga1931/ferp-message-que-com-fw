// module_timemgr.cpp
//
// ModuleTimeMgr — Real-time clock manager
//
// Boot sequence:
//   1. init() tries the DS1307 RTC immediately.
//   2. init() loads NVS backup (NVS is always ready at boot, no wait needed).
//   3. On MsgInternetStatus(connected=true): start NTP, enter NTP_SYNC.
//   4. On timer alarm in NTP_SYNC: poll pal_ntp_timesync_process().
//   5. On NTP done: set sys time, write RTC + NVS, enter READY.
//   6. In READY: 5-minute backup timer fires and writes NVS.

#include "module_timemgr.h"

#include "ds1307.hpp"
#include "pal_ntp.h"
#include "app_nvs.h"
#include "pal_logger.h"

#include "msg_internet_status.h"
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
#include "msg_timer_start_response.h"
#include "msg_timer_alarm.h"
#include "msg_time_status.h"

#include <string.h>
#include <sys/time.h>
#include <stdlib.h>

#define __TAG__   "MOD_TIME"
#define TIME_LOG  true

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const char    k_nvs_ns[]             = "timemgr";
static const char    k_nvs_key[]            = "epoch";
static const time_t  k_min_valid_epoch      = 1577836800LL;   /* 2020-01-01 UTC */
static const int64_t k_backup_interval_s    = 300LL;          /* 5 minutes */
static const uint32_t k_ntp_poll_ms         = 5000u;          /* 5 s */
static const uint32_t k_backup_interval_ms  = 300000u;        /* 5 min */

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModuleTimeMgr s_instance;
ModuleTimeMgr *ModuleTimeMgr::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void ModuleTimeMgr::init()
{
    subscribe(MsgInternetStatus::ID);
    subscribe(MsgTimerAlarm::ID);
    subscribe(MsgTimerStartResponse::ID);

    /* Try the hardware RTC first */
    _try_rtc();

    /* NVS is always available at boot — no need to wait for any mount event */
    app_nvs_init();
    _try_nvs_backup();

    bool valid = (_best_source != TIME_SOURCE_NONE);
    _publish_status(valid);

    _state = STATE_WAIT_INTERNET;
    LOG_MSG_INFO(TIME_LOG, "init: source=%d valid=%d", (int)_best_source, (int)valid);
}

// ---------------------------------------------------------------------------
// on_msg_received
// ---------------------------------------------------------------------------

void ModuleTimeMgr::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MsgInternetStatus::ID:
            _on_internet_status(msg);
            break;
        case MsgTimerAlarm::ID:
            _on_timer_alarm(msg);
            break;
        case MsgTimerStartResponse::ID:
            _on_timer_start_response(msg);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

void ModuleTimeMgr::_on_internet_status(const hsys_msg_t &msg)
{
    auto p = MsgInternetStatus::deserialize(msg);

    if (p.connected) {
        if (_state == STATE_WAIT_INTERNET || _state == STATE_READY) {
            _start_ntp_sync();
        }
    } else {
        if (_state == STATE_NTP_SYNC) {
            _stop_timer();
            pal_ntp_stop();
            _state = STATE_WAIT_INTERNET;
            LOG_MSG_INFO(TIME_LOG, "Internet lost during NTP sync -> WAIT_INTERNET");
        }
    }
}

void ModuleTimeMgr::_on_timer_alarm(const hsys_msg_t &msg)
{
    auto p = MsgTimerAlarm::deserialize(msg);
    if (p.source_module_id != MODULE_TIMEMGR_ID) return;

    switch (_state) {

        case STATE_NTP_SYNC: {
            int rc = pal_ntp_timesync_process();
            if (rc == 0) {
                time_t ntp_time = 0;
                pal_ntp_get_epoch_time(&ntp_time);
                _on_ntp_done(ntp_time);
            }
            break;
        }

        case STATE_READY:
            _write_nvs_backup();
            break;

        default:
            break;
    }
}

void ModuleTimeMgr::_on_timer_start_response(const hsys_msg_t &msg)
{
    auto p = MsgTimerStartResponse::deserialize(msg);
    if (p.source_module_id == MODULE_TIMEMGR_ID) {
        _timer_active = (p.result == TIMER_RESULT_OK);
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void ModuleTimeMgr::_try_rtc()
{
    ds1307_init_t  ds_init   = {};
    ds1307_handle_t ds_handle = {};

    if (ds1307_init(&ds_init, &ds_handle) != ERROR_DS1307_OK) {
        LOG_MSG_WARNING(TIME_LOG, "RTC: init failed");
        return;
    }

    time_t rtc_time = 0;
    if (ds1307_read_time(&rtc_time) != ERROR_DS1307_OK) {
        LOG_MSG_WARNING(TIME_LOG, "RTC: read failed");
        return;
    }

    if (rtc_time < k_min_valid_epoch) {
        LOG_MSG_WARNING(TIME_LOG, "RTC: time invalid (%ld)", (long)rtc_time);
        return;
    }

    _set_sys_time(rtc_time);
    _best_source = TIME_SOURCE_RTC;
    LOG_MSG_INFO(TIME_LOG, "RTC: loaded %ld", (long)rtc_time);
}

void ModuleTimeMgr::_try_nvs_backup()
{
    if (_best_source >= TIME_SOURCE_RTC) {
        /* RTC already set a better source — skip backup */
        return;
    }

    int64_t stored = 0;
    int32_t rc = app_nvs_read_i64(k_nvs_ns, k_nvs_key, &stored);
    if (rc == APP_NVS_ERR_NOT_FOUND) {
        LOG_MSG_INFO(TIME_LOG, "Backup: no NVS entry yet");
        return;
    }
    if (rc != APP_NVS_OK) {
        LOG_MSG_WARNING(TIME_LOG, "Backup: NVS read error (%ld)", (long)rc);
        return;
    }

    time_t t = (time_t)stored;
    if (t < k_min_valid_epoch) {
        LOG_MSG_WARNING(TIME_LOG, "Backup: invalid epoch %ld", (long)t);
        return;
    }

    _set_sys_time(t);
    _best_source = TIME_SOURCE_BACKUP;
    LOG_MSG_INFO(TIME_LOG, "Backup: loaded %ld from NVS", (long)t);
}

void ModuleTimeMgr::_start_ntp_sync()
{
    _state = STATE_NTP_SYNC;

    pal_ntp_init_default();
    pal_ntp_start();

    _arm_timer(k_ntp_poll_ms, /*repetitive=*/true);

    LOG_MSG_INFO(TIME_LOG, "NTP sync started");
}

void ModuleTimeMgr::_on_ntp_done(time_t ntp_time)
{
    if (ntp_time < k_min_valid_epoch) {
        LOG_MSG_WARNING(TIME_LOG, "NTP returned invalid time %ld", (long)ntp_time);
        return;
    }

    _set_sys_time(ntp_time);

    if (ds1307_set_time(ntp_time) != ERROR_DS1307_OK) {
        LOG_MSG_WARNING(TIME_LOG, "RTC: set_time failed after NTP sync");
    }

    _best_source = TIME_SOURCE_NTP;

    /* Force immediate NVS backup */
    _last_backup_epoch = 0;
    _write_nvs_backup();

    _publish_status(true);

    _stop_timer();
    _arm_timer(k_backup_interval_ms, /*repetitive=*/true);
    _state = STATE_READY;

    LOG_MSG_INFO(TIME_LOG, "NTP sync done: %ld -> READY", (long)ntp_time);
}

void ModuleTimeMgr::_publish_status(bool valid)
{
    MsgTimeStatus::Payload p{};
    time_t now = 0;
    time(&now);
    p.epoch  = valid ? now : 0;
    p.source = (uint8_t)_best_source;
    p.valid  = valid;

    hsys_msg_t *msg = MsgTimeStatus::create(id(), p);
    if (msg) publish(msg);
}

void ModuleTimeMgr::_write_nvs_backup()
{
    time_t now = 0;
    time(&now);

    if (llabs((long long)(now - _last_backup_epoch)) < k_backup_interval_s) return;

    _last_backup_epoch = now;

    int32_t rc = app_nvs_write_i64(k_nvs_ns, k_nvs_key, (int64_t)now);
    if (rc != APP_NVS_OK) {
        LOG_MSG_WARNING(TIME_LOG, "Backup: NVS write failed (%ld)", (long)rc);
        return;
    }
    LOG_MSG_INFO(TIME_LOG, "Backup: written epoch=%ld to NVS", (long)now);
}

void ModuleTimeMgr::_set_sys_time(time_t t)
{
    struct timeval tv;
    tv.tv_sec  = t;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

// ---------------------------------------------------------------------------
// Timer helpers
// ---------------------------------------------------------------------------

void ModuleTimeMgr::_arm_timer(uint32_t duration_ms, bool repetitive)
{
    _stop_timer();

    MsgTimerStart::Payload p{};
    p.source_module_id = id();
    p.start_offset_ms  = 0u;
    p.duration_ms      = duration_ms;
    p.is_repetitive    = repetitive;
    p.forced           = true;

    hsys_msg_t *msg = MsgTimerStart::create(id(), p);
    if (msg) publish(msg);
}

void ModuleTimeMgr::_stop_timer()
{
    if (!_timer_active) return;

    MsgTimerStop::Payload p{};
    p.source_module_id = id();

    hsys_msg_t *msg = MsgTimerStop::create(id(), p);
    if (msg) publish(msg);

    _timer_active = false;
}
