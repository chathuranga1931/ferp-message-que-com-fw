// module_cubesphere.h
//
// ModuleCubeSphere — fully message-driven CubeSphere cloud manager.
//
// All HTTP calls are made through ModuleHttp (http_task) using the
// HSYS HTTP session protocol.  No PAL HTTP calls are made directly.
//
// Top-level state machine:
//   WAIT_FOR_INTERNET ──[WiFi GOT_IP + cloud config ready]──► REGISTERING
//   REGISTERING ──[3-step reg OK]──► RUNNING
//   REGISTERING ──[fail]──► REGISTERING (retry via timer)
//   RUNNING ──[WiFi reconnect / events / heartbeat]──► RUNNING
//
// Registration sub-steps (within REGISTERING):
//   STEP_1: GET /bootstrap          → 401 + WWW-Authenticate nonce
//   STEP_2: GET /bootstrap (auth)   → 200 + device_id + secret
//   STEP_3: GET /device/config      → 200 + nozzle config
//
// HTTP session phases (used in both REGISTERING and RUNNING):
//   HTTP_IDLE       — no session active
//   HTTP_STARTING   — MsgHttpStartRequest sent, awaiting StartResponse
//   HTTP_EXECUTING  — all config + SendRequest burst-sent, awaiting Result

#pragma once

#include "hsys_module.h"
#include "msg_cubesphere_status.h"
#include "msg_config_wifi.h"
#include "msg_http_start_request.h"
#include "retransmission_manager.h"
#include "list_manager.h"
#include "msg_fuel_pumped.h"
#include "msg_nozzle_state.h"    // MsgNozzleState — pump-start trigger
#include "msg_fuel_print_status.h"  // MsgFuelPrintStatus — print status trigger
#include "app_module_ids.h"
#include <time.h>

#define MODULE_CUBESPHERE_NAME  "cubesph"

#define MODULE_CUBESPHERE_DEFAULT_HB_INTERVAL_MS  (60000UL)
#define MODULE_CUBESPHERE_RETRY_INTERVAL_MS        (60000UL)

// Internal size constants
#define CS_SIZE_UUID            50
#define CS_SIZE_SECRET         255
#define CS_SIZE_NOZZLE_ID       10
#define CS_SIZE_FUEL_TYPE       10
#define CS_SIZE_FUEL_TYPE_STR   35
#define CS_SIZE_WIFI_SSID       50
#define CS_SIZE_WIFI_PASSWORD   50
#define CS_SIZE_IP_ADDRESS      25
#define CS_SIZE_MAC             25
#define CS_SIZE_VERSION_STR     30
#define CS_SIZE_DEVICE_TYPE     30
#define CS_SIZE_STATUS_WORD     20
#define CS_NO_NOZZLES            2

typedef struct {
    char  agent_uuid[CS_SIZE_UUID];
    char  basic_authentication_base64[CS_SIZE_SECRET];
} cs_network_config_t;

typedef struct {
    char  nozzle_id[CS_SIZE_NOZZLE_ID];
    char  fuel_type[CS_SIZE_FUEL_TYPE];
    char  fuel_type_str[CS_SIZE_FUEL_TYPE_STR];
    char  uuid[CS_SIZE_UUID];
} cs_nozzle_config_t;

typedef struct {
    int8_t   rssi;
    uint32_t uptime_sec;
    uint32_t event_count_success;
    uint32_t event_count_failure;
} cs_hb_info_t;

typedef struct {
    char     ssid[CS_SIZE_WIFI_SSID];
    char     password[CS_SIZE_WIFI_PASSWORD];
    char     ip_address[CS_SIZE_IP_ADDRESS];
    char     mac_address_str[CS_SIZE_MAC];
    char     fw_version[CS_SIZE_VERSION_STR];
    char     hw_version[CS_SIZE_VERSION_STR];
    char     board_version[CS_SIZE_VERSION_STR];
    char     device_type[CS_SIZE_DEVICE_TYPE];
    char     sd_card_status[CS_SIZE_STATUS_WORD];
    int8_t   rssi;
    uint32_t uptime_sec;
    uint32_t event_count_success;
    uint32_t event_count_failure;
} cs_startup_info_t;

typedef struct {
    char     ssid[CS_SIZE_WIFI_SSID];
    char     password[CS_SIZE_WIFI_PASSWORD];
    char     ip_address[CS_SIZE_IP_ADDRESS];
    int8_t   rssi;
    uint32_t uptime_sec;
} cs_reconnect_info_t;

typedef struct {
    uint8_t  nozzle_idx;
    uint64_t time_stamp;
    uint32_t unit_pricex100;
    uint64_t total_pricex100;
    uint64_t volume_lx1000;
    uint32_t event_id;
} cs_pumped_event_t;

class ModuleCubeSphere : public HsysModule
{
public:
    ModuleCubeSphere() : HsysModule(MODULE_CUBESPHERE_ID, MODULE_CUBESPHERE_NAME) {}

    static ModuleCubeSphere *instance();

    void set_storage(const storage_interface_t *storage) { _storage = storage; }

    /**
     * Set the application-wide root CA certificate (from app_rootca.h).
     * Used as fallback when no per-session root CA is provided via cloud config.
     * Must be called before app_init() triggers init().
     * The pointer must remain valid for the lifetime of the module.
     */
    void set_root_ca(const char *ca) { _static_root_ca = ca; }

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    const storage_interface_t *_storage = nullptr;

    retx_manager_t  _retx_mgr   = {};
    bool            _retx_ready = false;

    // ── Top-level state ───────────────────────────────────────────────────────
    typedef enum {
        STATE_WAIT_FOR_INTERNET,
        STATE_REGISTERING,
        STATE_RUNNING,
    } cs_state_t;

    // ── HTTP session phase (active in REGISTERING and RUNNING) ────────────────
    typedef enum {
        HTTP_IDLE,      ///< No HTTP session
        HTTP_STARTING,  ///< StartRequest sent, awaiting StartResponse
        HTTP_EXECUTING, ///< SendRequest sent (burst), awaiting Result
    } http_phase_t;

    // ── Registration sub-step ─────────────────────────────────────────────────
    typedef enum {
        REG_STEP_1,  ///< GET /bootstrap — expect 401, capture nonce
        REG_STEP_2,  ///< GET /bootstrap with SAS-AC1 auth — expect 200
        REG_STEP_3,  ///< GET /device/config with Basic auth — expect 200
    } reg_step_t;

    // ── Running event type currently being sent ───────────────────────────────
    typedef enum {
        EVT_NONE,
        EVT_STARTUP,
        EVT_RECONNECT,
        EVT_HEARTBEAT,
        EVT_PUMPED,
        EVT_PUMPED_RETX,    ///< Retransmit of a previously failed pumped event
        EVT_TOTALIZED,      ///< app.fuel/totalized — same data as pump-end, different event
        EVT_PUMP_START,     ///< app.fuel/pump-start — nozzle lifted (pumping begins)
        EVT_PRINT_OK,       ///< app.fuel/printok — receipt printed successfully
        EVT_STATUS_UPDATE,
    } evt_type_t;

    cs_state_t   _state      = STATE_WAIT_FOR_INTERNET;
    http_phase_t _http_phase = HTTP_IDLE;
    reg_step_t   _reg_step   = REG_STEP_1;
    evt_type_t   _cur_evt    = EVT_NONE;

    // ── WiFi / internet context ───────────────────────────────────────────────
    bool    _cloud_config_ready = false;
    bool    _wifi_was_connected = false;
    bool    _wifi_reconnected   = false;
    bool    _internet_connected = false;   ///< Tracks last MsgInternetStatus value

    char   _wifi_ssid[CS_SIZE_WIFI_SSID]        = {};
    char   _wifi_password[CS_SIZE_WIFI_PASSWORD] = {};
    char   _wifi_ip[CS_SIZE_IP_ADDRESS]          = {};
    char   _wifi_mac[CS_SIZE_MAC]                = {};
    int8_t _wifi_rssi                            = -100;

    const char *_cloud_root_ca  = nullptr;  ///< From cloud config (runtime override)
    const char *_static_root_ca = nullptr;  ///< From app_rootca.h (set via set_root_ca())
    uint32_t    _uptime_sec     = 0;

    // ── System status (from ModuleMsgTranslator) ──────────────────────────────
    bool     _system_is_idle         = true;    ///< Updated by MSG_ID_SYSTEM_STATUS
    uint32_t _retx_check_countdown   = 10;      ///< Ticks remaining until first retransmit check (10 s after RUNNING; resets to 60 s)
    bool     _retx_in_progress       = false;   ///< True while an EVT_PUMPED_RETX HTTP send is live
    bool     _retx_last_send_failed  = false;   ///< Set when last retransmit attempt failed; cleared at next check

    // Read handle kept while _retx_in_progress is true; used to ack the list item on success
    list_mgr_read_handle_t _retx_pending_rh = {};

    uint32_t _pumped_success = 0;
    uint32_t _pumped_failure = 0;

    // ── Pending events (running state) ────────────────────────────────────────
    bool _pending_startup       = false;
    bool _pending_reconnect     = false;
    bool _pending_heartbeat     = false;
    bool _pending_status_update = false;
    bool _pending_pump_start[CS_NO_NOZZLES] = {}; ///< nozzle went to PUMPING
    bool _pending_print_ok      = false;
    bool _pending_totalized     = false;

    uint32_t _hb_interval_ms   = MODULE_CUBESPHERE_DEFAULT_HB_INTERVAL_MS;
    bool     _hb_enabled        = true;

    // ── Cached data for follow-up events ─────────────────────────────────────
    // Pump-start: nozzle index that just went PUMPING
    uint8_t  _pump_start_nozzle_idx = 0;
    // Totalized and print-ok share the last completed pump-end transaction.
    uint8_t  _last_pumped_nozzle_idx   = 0;
    uint32_t _last_pumped_vol_lx1000   = 0;
    uint32_t _last_pumped_unit_px100   = 0;
    uint64_t _last_pumped_total_px100  = 0;
    time_t   _last_pumped_ts_epoch     = 0;
    uint32_t       _print_ok_dispenser_event_id = 0;
    int64_t        _print_ok_timestamp_epoch    = 0;
    uint8_t        _print_ok_nozzle_idx         = 0;
    print_status_t _print_ok_status             = PRINT_STATUS_OK;
    uint64_t       _print_ok_ne_id              = 0;  ///< NE_ID from MsgFuelPrintStatus

    // ── CubeSphere cloud credentials ──────────────────────────────────────────
    static const char   _cs_key[];
    char                _reg_nonce[128]                 = {};  ///< Captured from WWW-Authenticate in step 1
    char                _event_json[2048]               = {};  ///< Pre-built JSON body for current event
    char                _auth_hdr[512]                  = {};  ///< Scratch: SAS-AC1 / Basic auth header value
    cs_network_config_t _cs_net_cfg                     = {};
    cs_nozzle_config_t  _cs_nozzles[CS_NO_NOZZLES]      = {};

    // ── Notification handlers ─────────────────────────────────────────────────
    void _on_config_ready();
    void _on_config_cloud(const hsys_msg_t &msg);
    void _on_config_wifi(const hsys_msg_t &msg);
    void _on_wifi_event(const hsys_msg_t &msg);
    void _on_internet_status(const hsys_msg_t &msg);
    void _on_fuel_pumped(const hsys_msg_t &msg);
    void _on_nozzle_state(const hsys_msg_t &msg);    ///< pump-start trigger
    void _on_fuel_print_ok(const hsys_msg_t &msg);   ///< printok trigger
    void _on_timer_alarm();
    void _on_tick();
    void _on_sd_ready();
    void _on_system_status(const hsys_msg_t &msg);

    // ── HTTP response handlers (DIRECT from ModuleHttp) ───────────────────────
    void _on_http_start_response(const hsys_msg_t &msg);
    void _on_http_response_header(const hsys_msg_t &msg);
    void _on_http_result(const hsys_msg_t &msg);

    // ── Registration helpers ──────────────────────────────────────────────────
    void _start_registration();
    void _start_reg_step_1();
    void _start_reg_step_2();
    void _start_reg_step_3();
    void _on_reg_step1_result(const hsys_msg_t &msg);
    void _on_reg_step2_result(const hsys_msg_t &msg);
    void _on_reg_step3_result(const hsys_msg_t &msg);
    void _reg_failed();

    // ── Running event helpers ─────────────────────────────────────────────────
    void _start_next_event();
    bool _build_event_json(evt_type_t evt);
    void _on_event_result(const hsys_msg_t &msg);

    // ── HTTP session helpers ──────────────────────────────────────────────────
    void _send_http_start(pal_http_method_t method, uint32_t timeout_ms,
                          const char *collect_key = nullptr);
    void _burst_get(const char *url);
    void _burst_get_with_auth(const char *url, const char *auth_value);
    void _burst_post(const char *url, const char *auth_value,
                     const char *json_body, uint32_t json_len);

    // ── Timer helper ─────────────────────────────────────────────────────────
    void _arm_timer(uint32_t duration_ms);

    // ── Crypto / time helpers ─────────────────────────────────────────────────
    static void _cs_get_sha256_hex(const uint8_t *data, size_t len,
                                    char *out, size_t out_len);
    static void _cs_calc_sha256(const char *nonce, const char *mac,
                                 const char *key, char *out, size_t out_len);
    static void _cs_format_iso8601(time_t epoch_sec, const char *tz_offset,
                                    char *buf, size_t buf_len);

    // ── Payload builders ──────────────────────────────────────────────────────
    cs_startup_info_t _build_startup_info() const;
    cs_hb_info_t      _build_hb_info()      const;

    // ── Status publisher ──────────────────────────────────────────────────────
    void _publish_status(cubesphere_status_event_t ev, uint8_t nozzle_idx = 0,
                         const char *uuid = nullptr);

    // ── Retransmission ────────────────────────────────────────────────────────
    void _retx_init();
    void _retx_store_pumped(const MsgFuelPumped::Payload &p);
    void _retx_process_one();
    bool _retx_try_send_one();   ///< Dequeue one event and push it to the HTTP send pipeline. Returns true if a send was started.

    // ── Common JSON builder ───────────────────────────────────────────────────
    /// Build the CubeSphere pump-end event JSON into _event_json.
    /// ne_id == 0 → omit ID/ABS_ID fields.
    /// Returns false if the nozzle is unconfigured or serialisation fails.
    bool _build_pumped_event_json(uint8_t  nozzle_idx,
                                  uint32_t vol_lx1000,
                                  uint32_t unit_pricex100,
                                  uint64_t total_pricex100,
                                  time_t   ts_epoch,
                                  uint32_t event_id = 0,
                                  uint64_t ne_id    = 0);
};
