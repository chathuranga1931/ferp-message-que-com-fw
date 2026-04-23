// module_timemgr.cpp
//
// ModuleTimeMgr — Real-time clock manager
//
// Boot sequence:
//   1. init() tries the DS1307 RTC immediately.
//   2. A 20-second one-shot timer guards the SPIFFS-ready wait.
//   3. On MsgSpiffsReady: load backup, publish status, enter WAIT_INTERNET.
//   4. On MsgInternetStatus(connected=true): start NTP, enter NTP_SYNC.
//   5. On timer alarm in NTP_SYNC: poll pal_ntp_timesync_process().
//   6. On NTP done: set sys time, write RTC + SPIFFS, enter READY.
//   7. In READY: 5-minute backup timer fires and writes timemgr.bin.

#include "module_timemgr.h"

#include "ds1307.hpp"
#include "pal_ntp.h"
#include "pal_spiffs.h"
#include "pal_logger.h"

#include "msg_spiffs_ready.h"
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

static const char   k_backup_file[]         = "timemgr.bin";
static const time_t k_min_valid_epoch       = 1577836800LL;   /* 2020-01-01 UTC */
static const int64_t k_backup_interval_s    = 300LL;          /* 5 minutes */
static const uint32_t k_spiffs_timeout_ms   = 20000u;         /* 20 s */
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
    subscribe(MsgSpiffsReady::ID);
    subscribe(MsgInternetStatus::ID);
    subscribe(MsgTimerAlarm::ID);
    subscribe(MsgTimerStartResponse::ID);

    /* Try the RTC immediately — it doesn't depend on SPIFFS */
    _try_rtc();

    /* Guard: if SPIFFS never mounts, time out after 20 s */
    _arm_timer(k_spiffs_timeout_ms, /*repetitive=*/false);

    _state = STATE_WAIT_SPIFFS;
    LOG_MSG_INFO(TIME_LOG, "init: state=WAIT_SPIFFS rtc_source=%d", (int)_best_source);
}

// ---------------------------------------------------------------------------
// on_msg_received
// ---------------------------------------------------------------------------

void ModuleTimeMgr::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MsgSpiffsReady::ID:
            _on_spiffs_ready(msg);
            break;
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

void ModuleTimeMgr::_on_spiffs_ready(const hsys_msg_t &)
{
    if (_state != STATE_WAIT_SPIFFS) return;

    _stop_timer();
    _try_spiffs_backup();

    bool valid = (_best_source != TIME_SOURCE_NONE);
    _publish_status(valid);

    _state = STATE_WAIT_INTERNET;
    LOG_MSG_INFO(TIME_LOG, "SPIFFS ready: source=%d valid=%d", (int)_best_source, (int)valid);
}

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

        case STATE_WAIT_SPIFFS:
            /* SPIFFS never mounted — work with what we have */
            _stop_timer();
            LOG_MSG_WARNING(TIME_LOG, "SPIFFS timeout, using source=%d", (int)_best_source);
            _publish_status(_best_source != TIME_SOURCE_NONE);
            _state = STATE_WAIT_INTERNET;
            break;

        case STATE_NTP_SYNC: {
            int rc = pal_ntp_timesync_process();
            if (rc == 0) {
                time_t ntp_time = 0;
                pal_ntp_get_epoch_time(&ntp_time);
                _on_ntp_done(ntp_time);
            }
            /* rc > 0 = still in progress; keep the repetitive timer running */
            break;
        }

        case STATE_READY:
            _write_spiffs_backup();
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

void ModuleTimeMgr::_try_spiffs_backup()
{
    if (_best_source >= TIME_SOURCE_RTC) {
        /* RTC already set a better source — skip backup */
        return;
    }

    uint8_t buf[sizeof(time_t)];
    size_t  n = 0;

    if (pal_spiffs_file_read(k_backup_file, buf, sizeof(buf), &n) != PAL_OK
            || n < sizeof(time_t)) {
        LOG_MSG_INFO(TIME_LOG, "Backup: no file or short read");
        return;
    }

    time_t t = 0;
    memcpy(&t, buf, sizeof(t));

    if (t < k_min_valid_epoch) {
        LOG_MSG_WARNING(TIME_LOG, "Backup: invalid epoch %ld", (long)t);
        return;
    }

    _set_sys_time(t);
    _best_source = TIME_SOURCE_BACKUP;
    LOG_MSG_INFO(TIME_LOG, "Backup: loaded %ld", (long)t);
}

void ModuleTimeMgr::_start_ntp_sync()
{
    _state = STATE_NTP_SYNC;

    pal_ntp_init_default();
    pal_ntp_start();

    /* Arm a repetitive poll timer; on mac the first call returns done */
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

    /* Write back to RTC so battery-backed time is accurate */
    if (ds1307_set_time(ntp_time) != ERROR_DS1307_OK) {
        LOG_MSG_WARNING(TIME_LOG, "RTC: set_time failed after NTP sync");
    }

    _best_source = TIME_SOURCE_NTP;

    /* Force immediate SPIFFS backup */
    _last_backup_epoch = 0;
    _write_spiffs_backup();

    _publish_status(true);

    /* Switch to 5-minute backup timer */
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

void ModuleTimeMgr::_write_spiffs_backup()
{
    time_t now = 0;
    time(&now);

    if (llabs((long long)(now - _last_backup_epoch)) < k_backup_interval_s) return;

    _last_backup_epoch = now;

    if (pal_spiffs_file_write(k_backup_file, (const uint8_t *)&now, sizeof(now)) != PAL_OK) {
        LOG_MSG_WARNING(TIME_LOG, "Backup: write failed");
        return;
    }
    LOG_MSG_INFO(TIME_LOG, "Backup: written epoch=%ld", (long)now);
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
