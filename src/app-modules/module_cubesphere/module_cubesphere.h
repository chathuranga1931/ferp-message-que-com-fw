// module_cubesphere.h
//
// ModuleCubeSphere — HTTPS cloud manager with all CubeSphere HTTP logic inlined.
//
// All HTTP session management, registration, and telemetry sending is
// implemented directly in this module. There is no external cube_sphere_api
// dependency.
//
// State machine:
//   WAIT_FOR_INTERNET ──[internet connected]──▶ REGISTERING
//   REGISTERING ──[_cs_register success]──▶ RUNNING
//   REGISTERING ──[_cs_register fail]──▶ REGISTERING (retry via timer)
//   RUNNING ──[internet lost]──▶ WAIT_FOR_INTERNET

#pragma once

#include "hsys_module.h"
#include "msg_cubesphere_status.h"
#include "msg_config_wifi.h"
#include "retransmission_manager.h"
#include "msg_fuel_pumped.h"
#include "pal_http_client.h"

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

#define CS_ERROR_OK                         0
#define CS_ERROR_INVALID_MAC                4
#define CS_ERROR_NO_NONCE                   5
#define CS_ERROR_GET_AGENT_CONFIG_FAILED    6
#define CS_ERROR_GET_NOZZLE_CONFIG_FAILED   7

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
    char    ssid[CS_SIZE_WIFI_SSID];
    char    password[CS_SIZE_WIFI_PASSWORD];
    char    ip_address[CS_SIZE_IP_ADDRESS];
    int8_t  rssi;
    uint32_t uptime_sec;
} cs_reconnect_info_t;

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

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    const storage_interface_t *_storage = nullptr;

    retx_manager_t  _retx_mgr   = {};
    bool            _retx_ready = false;

    typedef enum {
        STATE_WAIT_FOR_INTERNET,
        STATE_REGISTERING,
        STATE_RUNNING,
    } cubesphere_state_t;

    cubesphere_state_t _state = STATE_WAIT_FOR_INTERNET;

    bool    _internet_up        = false;
    bool    _cloud_config_ready = false;
    bool    _wifi_reconnected   = false;
    bool    _wifi_was_connected = false;

    char        _wifi_ssid[CS_SIZE_WIFI_SSID]         = {};
    char        _wifi_password[CS_SIZE_WIFI_PASSWORD]  = {};
    const char *_cloud_root_ca                         = nullptr;
    char        _wifi_ip[CS_SIZE_IP_ADDRESS]           = {};
    char        _wifi_mac[CS_SIZE_MAC]                 = {};
    int8_t      _wifi_rssi                             = -100;
    uint32_t    _uptime_sec                            = 0;

    uint32_t _pumped_success = 0;
    uint32_t _pumped_failure = 0;

    bool _pending_startup       = false;
    bool _pending_reconnect     = false;
    bool _pending_heartbeat     = false;
    bool _pending_status_update = false;

    uint32_t _hb_interval_ms = MODULE_CUBESPHERE_DEFAULT_HB_INTERVAL_MS;
    bool     _hb_enabled     = true;

    // CubeSphere cloud state (formerly cube_sphere_api.cpp module-level statics)
    static const char   _cs_key[];
    cs_network_config_t _cs_net_cfg                  = {};
    cs_nozzle_config_t  _cs_nozzles[CS_NO_NOZZLES]  = {};

    // Message handlers
    void _on_config_ready();
    void _on_config_cloud(const hsys_msg_t &msg);
    void _on_config_wifi(const hsys_msg_t &msg);
    void _on_wifi_event(const hsys_msg_t &msg);
    void _on_internet_status(const hsys_msg_t &msg);
    void _on_fuel_pumped(const hsys_msg_t &msg);
    void _on_timer_alarm();
    void _on_tick();
    void _on_sd_ready();

    // State machine helpers
    void _attempt_registration();
    void _process_events();
    void _arm_timer(uint32_t duration_ms);

    // CubeSphere HTTP session methods (inlined from cube_sphere_api.cpp)
    int32_t _cs_register(const char *mac12, const char *root_ca);
    int32_t _cs_send_event(const char *json_payload);
    int32_t _cs_send_hb(const cs_hb_info_t &hb);
    int32_t _cs_send_startup(const cs_startup_info_t &info);
    int32_t _cs_send_reconnect(const cs_reconnect_info_t &r);
    int32_t _cs_send_pumped(const cs_pumped_event_t &ev);
    int32_t _cs_send_status_updated(const cs_startup_info_t &info);

    // Crypto / time helpers (inlined from cube_sphere_api.cpp)
    static void _cs_get_sha256_hex(const uint8_t *data, size_t len, char *out, size_t out_len);
    static void _cs_calc_sha256(const char *nonce, const char *mac, const char *key,
                                 char *out, size_t out_len);
    static void _cs_format_iso8601(time_t epoch_sec, const char *tz_offset,
                                    char *buf, size_t buf_len);

    // Payload builders
    cs_startup_info_t _build_startup_info() const;
    cs_hb_info_t      _build_hb_info()      const;

    // Retransmission
    void _retx_init();
    void _retx_store_pumped(const MsgFuelPumped::Payload &p, const char *json_payload);
    void _retx_process_one();

    void _publish_status(cubesphere_status_event_t ev, uint8_t nozzle_idx = 0,
                         const char *uuid = nullptr);
};
