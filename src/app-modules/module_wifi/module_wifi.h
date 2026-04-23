// module_wifi.h
//
// ModuleWifi — WiFi connection manager.
//
// State machine:
//
//   WAIT_FOR_CONFIG ──[MsgConfigReady]──────────────────────────► (send MsgConfigGetWifi)
//   WAIT_FOR_CONFIG ──[MsgConfigWifi DIRECT]────────────────────► CONNECTING
//   CONNECTING      ──[PAL callback: STA_CONNECTED]─────────────► CONNECTING  (wait for IP)
//   CONNECTING      ──[PAL callback: STA_GOT_IP]────────────────► CONNECTED
//   CONNECTED       ──[PAL callback: STA_DISCONNECTED]──────────► RECONNECTING
//   RECONNECTING    ──[MsgTimerAlarm]────────────────────────────► CONNECTING  (retry)
//
// All PAL WiFi events (connected, got_ip, disconnected, rssi_changed)
// are converted to MsgWifiEvent broadcasts on the HSYS bus.
//
// Subscribed messages:
//   MsgConfigReady, MsgConfigWifi (DIRECT), MsgTimerAlarm
//
// Published messages:
//   MsgConfigGetWifi, MsgWifiEvent

#pragma once

#include "hsys_module.h"
#include "pal_wifi.h"
#include "msg_wifi_event.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_WIFI_NAME  "wifi"

// Retry interval on disconnect (ms)
#define MODULE_WIFI_RETRY_INTERVAL_MS  (10000UL)

// ---------------------------------------------------------------------------
// ModuleWifi
// ---------------------------------------------------------------------------

class ModuleWifi : public HsysModule
{
public:
    ModuleWifi() : HsysModule(MODULE_WIFI_ID, MODULE_WIFI_NAME) {}

    static ModuleWifi *instance();

protected:
    void init()                                 override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    // ── State machine ─────────────────────────────────────────────────────────
    typedef enum {
        STATE_WAIT_FOR_CONFIG,  ///< Waiting for MsgConfigReady + MsgConfigWifi
        STATE_CONNECTING,       ///< pal_wifi_sta_connect() called, awaiting IP
        STATE_CONNECTED,        ///< GOT_IP received, running
        STATE_RECONNECTING,     ///< Disconnected, waiting retry timer
    } wifi_state_t;

    wifi_state_t _state = STATE_WAIT_FOR_CONFIG;

    // ── Cached config ─────────────────────────────────────────────────────────
    char _ssid    [64] = {};
    char _password[64] = {};

    // ── Timer slot ────────────────────────────────────────────────────────────
    bool _timer_active = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void _start_connect();
    void _arm_retry_timer();
    void _publish_wifi_event(wifi_event_id_t event,
                             int8_t rssi        = -100,
                             const char *ip     = "",
                             const char *ssid   = "",
                             const char *mac    = "");

    // Message handlers
    void _on_config_wifi(const hsys_msg_t &msg);
    void _on_timer_alarm(const hsys_msg_t &msg);

    // PAL event callback (static → instance dispatch via user_data)
    static void _pal_wifi_cb(pal_wifi_event_t event, void *event_data, void *user_data);
    void        _on_pal_event(pal_wifi_event_t event);
};
