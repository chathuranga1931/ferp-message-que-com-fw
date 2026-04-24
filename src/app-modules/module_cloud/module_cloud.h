// module_cloud.h
//
// ModuleCloud — HTTPS cloud manager.
//
// State machine:
//   WAIT_FOR_INTERNET ──[internet connected]──▶ REGISTERING
//   REGISTERING ──[cloud_driver register_device success]──▶ RUNNING
//   REGISTERING ──[register_device fail]──▶ REGISTERING (retry via timer)
//   RUNNING ──[internet lost]──▶ WAIT_FOR_INTERNET
//
// Cloud backend:
//   Calls through the cloud_driver_t abstraction set via set_driver().
//   The concrete driver (e.g. cube_sphere) is wired in app.cpp before app_init().
//
// Retransmission handoff (future):
//   On PUMPED_FAILED, the module currently publishes MsgCloudStatus.
//   When ModuleRetransmit exists, also publish MsgRetransmitStore here.

#pragma once

#include "hsys_module.h"
#include "cloud_driver.h"
#include "msg_cloud_status.h"
#include "msg_config_wifi.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_CLOUD_NAME  "cloud"

// Default heartbeat interval — can be overridden via config
#define MODULE_CLOUD_DEFAULT_HB_INTERVAL_MS  (60000UL)

// Retry interval when registration fails
#define MODULE_CLOUD_RETRY_INTERVAL_MS       (60000UL)

// ---------------------------------------------------------------------------
// ModuleCloud
// ---------------------------------------------------------------------------

class ModuleCloud : public HsysModule
{
public:
    ModuleCloud() : HsysModule(MODULE_CLOUD_ID, MODULE_CLOUD_NAME) {}

    static ModuleCloud *instance();

    /** Wire the cloud backend before app_init(). Must be called before init(). */
    void set_driver(const cloud_driver_t *drv) { _drv = drv; }

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    const cloud_driver_t *_drv            = nullptr;  ///< Cloud backend — set via set_driver()

    // ── State machine ─────────────────────────────────────────────────────────
    typedef enum {
        STATE_WAIT_FOR_INTERNET,
        STATE_REGISTERING,
        STATE_RUNNING,
    } cloud_state_t;

    cloud_state_t _state = STATE_WAIT_FOR_INTERNET;

    // ── Connectivity tracking ─────────────────────────────────────────────────
    bool    _internet_up           = false;
    bool    _cloud_config_ready    = false;  ///< true once MsgConfigCloud received
    bool    _wifi_reconnected      = false;  ///< true when wifi lost + re-got IP
    bool    _wifi_was_connected    = false;

    // ── Cached runtime info (populated from messages) ─────────────────────────
    char    _wifi_ssid[50]         = {};   ///< Connected SSID  — from MsgWifiEvent
    char          _wifi_password[50] = {};       ///< WiFi password  — from MsgConfigWifi
    const char   *_cloud_root_ca    = nullptr;  ///< PEM root-CA pointer — from MsgConfigCloud (static lifetime)
    char    _wifi_ip[25]           = {};
    char    _wifi_mac[25]          = {};
    int8_t  _wifi_rssi             = -100;
    uint32_t _uptime_sec           = 0;  ///< incremented on MSG_TICK_1000MS

    // ── Event counters ────────────────────────────────────────────────────────
    uint32_t _pumped_success = 0;
    uint32_t _pumped_failure = 0;

    // ── Pending event flags (set by messages, consumed in _process_events) ────
    bool _pending_startup       = false;
    bool _pending_reconnect     = false;
    bool _pending_heartbeat     = false;
    bool _pending_status_update = false;

    // ── Config ────────────────────────────────────────────────────────────────
    uint32_t _hb_interval_ms   = MODULE_CLOUD_DEFAULT_HB_INTERVAL_MS;
    bool     _hb_enabled       = true;

    // ── Helpers ───────────────────────────────────────────────────────────────
    void _on_config_ready();
    void _on_config_cloud(const hsys_msg_t &msg);
    void _on_config_wifi(const hsys_msg_t &msg);
    void _on_wifi_event(const hsys_msg_t &msg);
    void _on_internet_status(const hsys_msg_t &msg);
    void _on_fuel_pumped(const hsys_msg_t &msg);
    void _on_timer_alarm();
    void _on_tick();

    void _attempt_registration();
    void _process_events();

    void _arm_timer(uint32_t duration_ms);

    cloud_startup_info_t _build_startup_info() const;
    cloud_hb_info_t      _build_hb_info()      const;

    void _publish_cloud_status(cloud_status_event_t ev, uint8_t nozzle_idx = 0);
};
