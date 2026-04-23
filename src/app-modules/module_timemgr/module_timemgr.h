// module_timemgr.h
//
// ModuleTimeMgr — Real-time clock manager
//
// Responsibilities:
//   1. On boot: try DS1307 RTC immediately.
//   2. On MsgSpiffsReady: load SPIFFS backup as fallback (if RTC unavailable).
//   3. On internet connection: synchronise via NTP (highest priority source).
//   4. Every 5 minutes: save a backup of the current time to SPIFFS.
//   5. Publish MsgTimeStatus whenever the time source changes.
//
// Source priority (lowest → highest):
//   NONE → BACKUP (SPIFFS) → RTC (DS1307) → NTP

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "hsys_msg.h"
#include "msg_time_status.h"   // time_source_t
#include <time.h>

class ModuleTimeMgr : public HsysModule
{
public:
    ModuleTimeMgr() : HsysModule(MODULE_TIMEMGR_ID, "timemgr") {}

    static ModuleTimeMgr *instance();

protected:
    void init() override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    // -----------------------------------------------------------------------
    // State machine
    // -----------------------------------------------------------------------

    typedef enum {
        STATE_WAIT_SPIFFS,     ///< Waiting for SPIFFS to mount
        STATE_WAIT_INTERNET,   ///< SPIFFS/RTC done; waiting for internet
        STATE_NTP_SYNC,        ///< NTP synchronisation in progress
        STATE_READY,           ///< Fully synchronised; periodic backup running
    } state_t;

    state_t      _state       = STATE_WAIT_SPIFFS;
    time_source_t _best_source = TIME_SOURCE_NONE;
    time_t        _last_backup_epoch = 0;
    bool          _timer_active = false;

    // -----------------------------------------------------------------------
    // Message handlers
    // -----------------------------------------------------------------------

    void _on_spiffs_ready(const hsys_msg_t &msg);
    void _on_internet_status(const hsys_msg_t &msg);
    void _on_timer_alarm(const hsys_msg_t &msg);
    void _on_timer_start_response(const hsys_msg_t &msg);

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    void _try_rtc();
    void _try_spiffs_backup();
    void _start_ntp_sync();
    void _on_ntp_done(time_t ntp_time);
    void _publish_status(bool valid);
    void _write_spiffs_backup();
    void _set_sys_time(time_t t);

    void _arm_timer(uint32_t duration_ms, bool repetitive);
    void _stop_timer();
};
