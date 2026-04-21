// module_wifi.cpp
//
// ModuleWifi — WiFi connection manager.
// See module_wifi.h for the state machine description.

#include "module_wifi.h"

#include "msg_config_ready.h"
#include "msg_config_get_wifi.h"
#include "msg_config_wifi.h"
#include "msg_timer_start.h"
#include "msg_timer_stop.h"
#include "msg_timer_start_response.h"
#include "msg_timer_alarm.h"
#include "msg_wifi_event.h"
#include "pal_logger.h"
#include "pal_wifi.h"

#include <string.h>

#define __TAG__       "MOD_WIFI"
#define WIFI_LOG      true

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

static ModuleWifi s_instance;
ModuleWifi *ModuleWifi::instance() { return &s_instance; }

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void ModuleWifi::init()
{
    subscribe(MsgConfigReady::ID);
    subscribe(MsgConfigWifi::ID);         // DIRECT response from ModuleConfig
    subscribe(MsgTimerAlarm::ID);
    subscribe(MsgTimerStartResponse::ID);

    log("init — waiting for config");
}

// ---------------------------------------------------------------------------
// on_msg_received
// ---------------------------------------------------------------------------

void ModuleWifi::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MsgConfigReady::ID: {
            // Request our WiFi credentials from ModuleConfig
            MsgConfigGetWifi::Payload req{};
            req.source_module_id = id();
            hsys_msg_t *out = MsgConfigGetWifi::create(id(), req);
            if (out) publish(out);
            LOG_MSG_INFO(WIFI_LOG, "config ready — requested MsgConfigWifi");
            break;
        }

        case MsgConfigWifi::ID:
            _on_config_wifi(msg);
            break;

        case MsgTimerAlarm::ID:
            _on_timer_alarm(msg);
            break;

        case MsgTimerStartResponse::ID: {
            auto p = MsgTimerStartResponse::deserialize(msg);
            if (p.source_module_id == MODULE_WIFI_ID) {
                _timer_active = (p.result == TIMER_RESULT_OK);
            }
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Private — message handlers
// ---------------------------------------------------------------------------

void ModuleWifi::_on_config_wifi(const hsys_msg_t &msg)
{
    auto p = MsgConfigWifi::deserialize(msg);
    strncpy(_ssid,     p.ssid,     sizeof(_ssid)     - 1);
    strncpy(_password, p.password, sizeof(_password) - 1);

    LOG_MSG_INFO(WIFI_LOG, "config received:");
    LOG_MSG_INFO(WIFI_LOG, "  ssid     = \"%s\"", _ssid);
    LOG_MSG_INFO(WIFI_LOG, "  password = %s", (_password[0] != '\0') ? "***" : "(empty)");

    _start_connect();
}

void ModuleWifi::_on_timer_alarm(const hsys_msg_t &msg)
{
    (void)msg;
    if (_state == STATE_RECONNECTING) {
        LOG_MSG_INFO(WIFI_LOG, "retry timer fired — reconnecting");
        _timer_active = false;
        _start_connect();
    }
}

// ---------------------------------------------------------------------------
// Private — connect
// ---------------------------------------------------------------------------

void ModuleWifi::_start_connect()
{
    _state = STATE_CONNECTING;
    LOG_MSG_INFO(WIFI_LOG, "connecting to \"%s\"…", _ssid);

    pal_wifi_init_config_t cfg{};
    cfg.mode = PAL_WIFI_MODE_STA;
    strncpy(cfg.config.sta.ssid,     _ssid,     sizeof(cfg.config.sta.ssid)     - 1);
    strncpy(cfg.config.sta.password, _password, sizeof(cfg.config.sta.password) - 1);

    pal_wifi_init(&cfg, _pal_wifi_cb, this);
    pal_wifi_start();
    pal_wifi_sta_connect();
}

// ---------------------------------------------------------------------------
// Private — PAL callback (static → instance dispatch)
// ---------------------------------------------------------------------------

void ModuleWifi::_pal_wifi_cb(pal_wifi_event_t event, void * /*event_data*/, void *user_data)
{
    auto *self = static_cast<ModuleWifi *>(user_data);
    if (self) self->_on_pal_event(event);
}

void ModuleWifi::_on_pal_event(pal_wifi_event_t event)
{
    // Note: this may be called from a PAL background thread.
    // MsgXxx::create() + publish() are thread-safe in HSYS.

    switch (event)
    {
        case PAL_WIFI_EVENT_STA_CONNECTED: {
            LOG_MSG_INFO(WIFI_LOG, "STA_CONNECTED");
            _publish_wifi_event(WIFI_EVENT_STA_CONNECTED, -55, "", _ssid, "");
            break;
        }

        case PAL_WIFI_EVENT_STA_GOT_IP: {
            _state = STATE_CONNECTED;

            char ip[PAL_WIFI_IP_STR_LEN]  = {};
            char mac[PAL_WIFI_MAC_STR_LEN] = {};
            pal_wifi_get_ip_str(ip,  sizeof(ip));
            pal_wifi_get_mac_str(mac, sizeof(mac));

            int8_t rssi = -55;
            pal_wifi_sta_get_rssi(&rssi);

            LOG_MSG_INFO(WIFI_LOG, "GOT_IP ip=%s mac=%s rssi=%d", ip, mac, (int)rssi);
            _publish_wifi_event(WIFI_EVENT_STA_GOT_IP, rssi, ip, _ssid, mac);
            break;
        }

        case PAL_WIFI_EVENT_STA_DISCONNECTED: {
            LOG_MSG_INFO(WIFI_LOG, "STA_DISCONNECTED — arming retry timer");
            _state = STATE_RECONNECTING;
            _publish_wifi_event(WIFI_EVENT_STA_DISCONNECTED, -100, "", _ssid, "");
            _arm_retry_timer();
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Private — helpers
// ---------------------------------------------------------------------------

void ModuleWifi::_publish_wifi_event(wifi_event_id_t event,
                                      int8_t          rssi,
                                      const char     *ip,
                                      const char     *ssid,
                                      const char     *mac)
{
    MsgWifiEvent::Payload p{};
    p.event = event;
    p.rssi  = rssi;
    if (ip)   strncpy(p.ip_address,  ip,   sizeof(p.ip_address)  - 1);
    if (ssid) strncpy(p.ssid,        ssid, sizeof(p.ssid)        - 1);
    if (mac)  strncpy(p.mac_address, mac,  sizeof(p.mac_address) - 1);

    hsys_msg_t *msg = MsgWifiEvent::create(id(), p);
    if (msg) publish(msg);
}

void ModuleWifi::_arm_retry_timer()
{
    if (_timer_active) return;

    MsgTimerStart::Payload p{};
    p.source_module_id = MODULE_WIFI_ID;
    p.start_offset_ms  = 0;
    p.duration_ms      = MODULE_WIFI_RETRY_INTERVAL_MS;
    p.is_repetitive    = false;   // one-shot; we re-arm on reconnect if needed
    p.forced           = false;

    hsys_msg_t *msg = MsgTimerStart::create(id(), p);
    if (msg) publish(msg);
}
