// module_internet.cpp
//
// ModuleInternet implementation.
// See module_internet.h for architecture notes.

#include "module_internet.h"

#include "pal_network.h"
#include "pal_logger.h"

#include "msg_wifi_event.h"
#include "msg_internet_status.h"
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
#include "msg_timer_start_response.h"
#include "msg_timer_alarm.h"
#include "msg_ota_event.h"

#define __TAG__  "MOD_INT "

#define INT_LOG  true

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModuleInternet s_instance;
ModuleInternet *ModuleInternet::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void ModuleInternet::init()
{
    subscribe(MsgWifiEvent::ID);
    subscribe(MsgTimerAlarm::ID);
    subscribe(MsgTimerStartResponse::ID);
    subscribe(MsgOtaEvent::ID);

    log("init");
}

// ---------------------------------------------------------------------------
// on_msg_received
// ---------------------------------------------------------------------------

void ModuleInternet::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MsgWifiEvent::ID:
            _on_wifi_event(msg);
            break;

        case MsgTimerAlarm::ID:
            _on_timer_alarm(msg);
            break;

        case MsgOtaEvent::ID:
            _on_ota_event(msg);
            break;

        case MsgTimerStartResponse::ID: {
            auto p = MsgTimerStartResponse::deserialize(msg);
            if (p.source_module_id == MODULE_INTERNET_ID) {
                _timer_active = (p.result == TIMER_RESULT_OK);
                if (!_timer_active) {
                    LOG_MSG_WARNING(INT_LOG, "timer start failed (result=%d)", (int)p.result);
                }
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

void ModuleInternet::_on_wifi_event(const hsys_msg_t &msg)
{
    auto p = MsgWifiEvent::deserialize(msg);

    switch (p.event)
    {
        case WIFI_EVENT_STA_GOT_IP:
            LOG_MSG_INFO(INT_LOG, "WiFi got IP — start internet monitoring");
            if (_state != STATE_CHECKING) {
                _enter_checking();
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (_state == STATE_CHECKING) {
                LOG_MSG_INFO(INT_LOG, "WiFi lost — stop internet monitoring");
                _exit_checking();
            }
            break;

        default:
            break;
    }
}

void ModuleInternet::_on_timer_alarm(const hsys_msg_t &msg)
{
    auto p = MsgTimerAlarm::deserialize(msg);
    if (p.source_module_id != MODULE_INTERNET_ID) return;   // not our timer

    if (_state == STATE_CHECKING && !_ota_in_progress) {
        _run_ping_and_publish();
    }
}

void ModuleInternet::_on_ota_event(const hsys_msg_t &msg)
{
    auto p = MsgOtaEvent::deserialize(msg);

    if (p.event == OTA_EVENT_SESSION_STARTED) {
        _ota_in_progress = true;
        LOG_MSG_INFO(INT_LOG, "OTA session started — pausing internet ping checks");
        return;
    }

    if (p.event != OTA_EVENT_COMPLETE && p.event != OTA_EVENT_SESSION_ABORTED &&
        p.event != OTA_EVENT_TIMEOUT) {
        return;
    }
    if (!_ota_in_progress) return;

    _ota_in_progress = false;
    LOG_MSG_INFO(INT_LOG, "OTA session ended (event=%d) — resuming internet ping checks", (int)p.event);

    // Deliberately do NOT re-check immediately here. ModuleInternet shares
    // network_task with ModuleMqtt and OtaModule; pal_network_ping() blocks
    // that task for up to MODULE_INTERNET_PING_TIMEOUT_MS (2s). Calling it
    // inline from this notification handler delayed ModuleMqtt's delivery of
    // this very same MsgOtaEvent (and OtaModule's own reboot-countdown tick)
    // by that long, which pushed the ota_complete MQTT ack out past the
    // device's OTA_REBOOT_DELAY_MS reboot window — the host never received
    // it. Just let the next periodic timer tick (up to 60s) run the check.
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

void ModuleInternet::_enter_checking()
{
    _state = STATE_CHECKING;
    _run_ping_and_publish();   // immediate first check
    _arm_timer();              // then arm the repeating timer
}

void ModuleInternet::_exit_checking()
{
    _stop_timer();
    _state = STATE_WAIT_FOR_WIFI;

    // Always publish disconnected on WiFi loss
    if (_internet_up) {
        _publish_status(false);
    }
}

// ---------------------------------------------------------------------------
// Ping + publish
// ---------------------------------------------------------------------------

void ModuleInternet::_run_ping_and_publish()
{
    LOG_MSG_INFO(INT_LOG, "pinging %s ...", MODULE_INTERNET_PING_HOST);
    bool up = pal_network_ping(MODULE_INTERNET_PING_HOST,
                                MODULE_INTERNET_PING_TIMEOUT_MS);

    // Only publish on change — matches old app_processing() logic
    if (up != _internet_up) {
        _publish_status(up);
    }
}

void ModuleInternet::_publish_status(bool connected)
{
    _internet_up = connected;
    LOG_MSG_INFO(INT_LOG, "internet %s", connected ? "UP" : "DOWN");

    MsgInternetStatus::Payload p{};
    p.connected = connected;
    hsys_msg_t *msg = MsgInternetStatus::create(MODULE_INTERNET_ID, p);
    if (msg) publish(msg);
}

// ---------------------------------------------------------------------------
// Timer helpers
// ---------------------------------------------------------------------------

void ModuleInternet::_arm_timer()
{
    MsgTimerStart::Payload p{};
    p.source_module_id = MODULE_INTERNET_ID;
    p.start_offset_ms  = 0;
    p.duration_ms      = MODULE_INTERNET_CHECK_INTERVAL_MS;
    p.is_repetitive    = true;
    p.forced           = false;

    hsys_msg_t *msg = MsgTimerStart::create(MODULE_INTERNET_ID, p);
    if (msg) publish(msg);
}

void ModuleInternet::_stop_timer()
{
    if (!_timer_active) return;

    MsgTimerStop::Payload p{};
    p.source_module_id = MODULE_INTERNET_ID;

    hsys_msg_t *msg = MsgTimerStop::create(MODULE_INTERNET_ID, p);
    if (msg) publish(msg);

    _timer_active = false;
}
