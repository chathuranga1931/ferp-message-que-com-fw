// module_internet.h
//
// ModuleInternet — internet reachability monitor.
//
// Waits for MsgWifiEvent::GOT_IP, then periodically probes the network
// via pal_network_ping() and publishes MsgInternetStatus on any change.
//
// State machine:
//
//   WAIT_FOR_WIFI ──[GOT_IP]──────────────────────────────────► CHECKING
//   CHECKING      ──[MsgTimerAlarm]────────────────────────────► CHECKING  (repeat)
//   CHECKING      ──[MsgWifiEvent::STA_DISCONNECTED]──────────► WAIT_FOR_WIFI
//
// On entry to CHECKING: arm a repeating timer + run first ping immediately.
// On exit  from CHECKING: stop timer + publish MsgInternetStatus(false).
//
// Published messages:
//   MsgInternetStatus — on every connectivity change (not every poll).
//
// Subscribed messages:
//   MsgWifiEvent, MsgTimerAlarm

#pragma once

#include "hsys_module.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_INTERNET_NAME  "internet"

// Interval between ping checks while WiFi is up
#define MODULE_INTERNET_CHECK_INTERVAL_MS  (60000UL)

// Ping target and timeout
#define MODULE_INTERNET_PING_HOST          "8.8.8.8"
#define MODULE_INTERNET_PING_TIMEOUT_MS    (2000U)

// ---------------------------------------------------------------------------
// ModuleInternet
// ---------------------------------------------------------------------------

class ModuleInternet : public HsysModule
{
public:
    ModuleInternet() : HsysModule(MODULE_INTERNET_ID, MODULE_INTERNET_NAME) {}

    static ModuleInternet *instance();

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    // ── State machine ─────────────────────────────────────────────────────────
    typedef enum {
        STATE_WAIT_FOR_WIFI,
        STATE_CHECKING,
    } internet_state_t;

    internet_state_t _state       = STATE_WAIT_FOR_WIFI;

    // ── Connectivity tracking ─────────────────────────────────────────────────
    bool _internet_up             = false;   ///< last published state

    // While an OTA session is active, flash-erase stalls can make the ping
    // socket miss its 2s timeout even though the network is fine — a false
    // "internet DOWN" here makes ModuleMqtt call pal_mqtt_client_stop(),
    // which is far more disruptive than a plain MQTT-level reconnect.
    // Skip pinging (and leave _internet_up exactly as it was) while OTA is
    // in progress; re-check immediately once it ends.
    bool _ota_in_progress          = false;

    // ── Timer slot ────────────────────────────────────────────────────────────
    bool     _timer_active        = false;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void _enter_checking();
    void _exit_checking();
    void _run_ping_and_publish();
    void _arm_timer();
    void _stop_timer();
    void _publish_status(bool connected);

    // Message handlers
    void _on_wifi_event(const hsys_msg_t &msg);
    void _on_timer_alarm(const hsys_msg_t &msg);
    void _on_ota_event(const hsys_msg_t &msg);
};
