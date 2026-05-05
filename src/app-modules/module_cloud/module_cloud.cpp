// module_cloud.cpp
//
// ModuleCloud — HTTPS cloud manager.
//
// See module_cloud.h for the state machine description.

#include "module_cloud.h"
#include "msg_config_ready.h"
#include "msg_config_get_cloud.h"
#include "msg_config_get_wifi.h"
#include "msg_config_cloud.h"
#include "msg_config_wifi.h"
#include "msg_wifi_event.h"
#include "msg_internet_status.h"
#include "msg_cloud_status.h"
#include "msg_fuel_pumped.h"
#include "msg_timer_start.h"
#include "msg_timer_alarm.h"
#include "msg_sd_ready.h"
#include "pal_logger.h"
#include "pal_efuse.h"
#include "pal_time.h"

#define ERROR_OK  0

#include <string.h>

#define __TAG__       "CLOUD   "
#define CLOUD_LOG_EN  true

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleCloud s_instance;
ModuleCloud *ModuleCloud::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleCloud::init()
{
    subscribe(MsgConfigReady::ID);
    subscribe(MsgConfigCloud::ID);   // DIRECT response from ModuleConfig
    subscribe(MsgConfigWifi::ID);    // DIRECT response from ModuleConfig
    subscribe(MsgWifiEvent::ID);
    subscribe(MsgInternetStatus::ID);
    subscribe(MsgFuelPumped::ID);
    subscribe(MsgTimerAlarm::ID);
    subscribe(MsgSdReady::ID);
    subscribe(MSG_ID_TICK_1000MS);

    LOG_MSG_INFO(CLOUD_LOG_EN, "init — state=WAIT_FOR_INTERNET");
}

// ── Message handler ────────────────────────────────────────────────────────────

void ModuleCloud::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MsgConfigReady::ID:
            _on_config_ready();
            break;

        case MsgConfigCloud::ID:
            _on_config_cloud(msg);
            break;

        case MsgConfigWifi::ID:
            _on_config_wifi(msg);
            break;

        case MsgWifiEvent::ID:
            _on_wifi_event(msg);
            break;

        case MsgInternetStatus::ID:
            _on_internet_status(msg);
            break;

        case MsgFuelPumped::ID:
            _on_fuel_pumped(msg);
            break;

        case MsgSdReady::ID:
            _on_sd_ready();
            break;

        case MsgTimerAlarm::ID:
            _on_timer_alarm();
            break;

        case MSG_ID_TICK_1000MS:
            _on_tick();
            break;

        default:
            break;
    }
}

// ── Private — message handlers ────────────────────────────────────────────────

void ModuleCloud::_on_config_ready()
{
    // Request cloud config (root CA, heartbeat settings)
    MsgConfigGetCloud::Payload req{};
    req.source_module_id = id();
    hsys_msg_t *msg = MsgConfigGetCloud::create(id(), req);
    if (msg) publish(msg);

    // Request wifi config (ssid, password — for cloud telemetry payloads)
    MsgConfigGetWifi::Payload wreq{};
    wreq.source_module_id = id();
    hsys_msg_t *wmsg = MsgConfigGetWifi::create(id(), wreq);
    if (wmsg) publish(wmsg);
}

void ModuleCloud::_on_config_cloud(const hsys_msg_t &msg)
{
    auto p = MsgConfigCloud::deserialize(msg);

    _cloud_root_ca = p.root_ca;   // store pointer; string has static lifetime

    if (p.hb_interval_s > 0) {
        _hb_interval_ms = p.hb_interval_s * 1000UL;
    }
    _hb_enabled = p.hb_enabled;

    LOG_MSG_INFO(CLOUD_LOG_EN, "cloud config received:");
    LOG_MSG_INFO(CLOUD_LOG_EN, "  root_ca      = %s", _cloud_root_ca ? "***" : "(null)");
    LOG_MSG_INFO(CLOUD_LOG_EN, "  hb_enabled   = %d", (int)p.hb_enabled);
    LOG_MSG_INFO(CLOUD_LOG_EN, "  hb_interval  = %us", (unsigned)p.hb_interval_s);

    _cloud_config_ready = true;

    // If internet is already up, start registration now.
    // (Normal case: internet arrives later and triggers registration via _on_internet_status.)
    if (_internet_up && _state == STATE_WAIT_FOR_INTERNET) {
        LOG_MSG_INFO(CLOUD_LOG_EN, "config ready and internet already up — starting registration");
        _state = STATE_REGISTERING;
        _attempt_registration();
    }
}

void ModuleCloud::_on_config_wifi(const hsys_msg_t &msg)
{
    auto p = MsgConfigWifi::deserialize(msg);
    strncpy(_wifi_ssid,     p.ssid,     sizeof(_wifi_ssid)     - 1);
    strncpy(_wifi_password, p.password, sizeof(_wifi_password) - 1);
    LOG_MSG_INFO(CLOUD_LOG_EN, "wifi config received: ssid=\"%s\"", _wifi_ssid);
}

void ModuleCloud::_on_wifi_event(const hsys_msg_t &msg)
{
    auto p = MsgWifiEvent::deserialize(msg);

    switch (p.event) {
        case WIFI_EVENT_STA_GOT_IP:
            if (_wifi_was_connected) {
                // This is a reconnect after a drop
                _wifi_reconnected = true;
                if (_state == STATE_RUNNING) {
                    _pending_reconnect = true;
                }
            }
            _wifi_was_connected = true;
            _wifi_rssi = p.rssi;
            strncpy(_wifi_ssid, p.ssid,        sizeof(_wifi_ssid) - 1);
            strncpy(_wifi_ip,   p.ip_address,  sizeof(_wifi_ip)   - 1);
            strncpy(_wifi_mac,  p.mac_address, sizeof(_wifi_mac)  - 1);
            LOG_MSG_INFO(CLOUD_LOG_EN, "WiFi GOT_IP ssid=%s ip=%s rssi=%d", _wifi_ssid, _wifi_ip, _wifi_rssi);
            break;

        case WIFI_EVENT_STA_RSSI_CHANGED:
            _wifi_rssi = p.rssi;
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            _wifi_was_connected = true;  // mark so next connect is a reconnect
            LOG_MSG_INFO(CLOUD_LOG_EN, "WiFi disconnected");
            break;

        default:
            break;
    }
}

void ModuleCloud::_on_internet_status(const hsys_msg_t &msg)
{
    auto p = MsgInternetStatus::deserialize(msg);
    _internet_up = p.connected;

    LOG_MSG_INFO(CLOUD_LOG_EN, "internet %s", p.connected ? "UP" : "DOWN");

    if (p.connected) {
        if (_state == STATE_WAIT_FOR_INTERNET) {
            if (_cloud_config_ready) {
                _state = STATE_REGISTERING;
                _attempt_registration();
            } else {
                LOG_MSG_INFO(CLOUD_LOG_EN, "internet up but cloud config not yet received — waiting");
            }
        }
        // If already RUNNING, events continue flowing normally
    } else {
        if (_state == STATE_RUNNING) {
            LOG_MSG_WARNING(CLOUD_LOG_EN, "internet lost — returning to WAIT_FOR_INTERNET");
            _state = STATE_WAIT_FOR_INTERNET;
        }
    }
}

void ModuleCloud::_on_fuel_pumped(const hsys_msg_t &msg)
{
    auto p = MsgFuelPumped::deserialize(msg);

    if (_state != STATE_RUNNING) {
        LOG_MSG_WARNING(CLOUD_LOG_EN, "fuel pumped but not running — storing for retransmission");
        _pumped_failure++;
        _publish_cloud_status(CLOUD_STATUS_PUMPED_FAILED, p.nozzle_idx);
        _retx_store_pumped(p, nullptr);
        return;
    }

    cloud_pumped_info_t info = {};
    info.nozzle_idx      = p.nozzle_idx;
    info.time_stamp      = (uint64_t)time(nullptr);
    info.unit_pricex100  = p.unit_pricex100;
    info.total_pricex100 = p.total_pricex100;
    info.volume_lx1000   = p.vol_lx1000;
    info.event_id        = _pumped_success + _pumped_failure + 1;

    LOG_MSG_INFO(CLOUD_LOG_EN, "sending pumped event nozzle=%u vol=%lu",
                 p.nozzle_idx, (unsigned long)p.vol_lx1000);

    int32_t ret = _drv ? _drv->send_pumped(info) : -1;
    if (ret == ERROR_OK) {
        _pumped_success++;
        LOG_MSG_INFO(CLOUD_LOG_EN, "pumped sent OK (success=%lu)", (unsigned long)_pumped_success);
        _publish_cloud_status(CLOUD_STATUS_PUMPED_SUCCESS, p.nozzle_idx);
    } else {
        _pumped_failure++;
        LOG_MSG_ERROR(CLOUD_LOG_EN, "pumped send FAILED ret=%d — storing for retransmission", ret);
        _publish_cloud_status(CLOUD_STATUS_PUMPED_FAILED, p.nozzle_idx);
        _retx_store_pumped(p, nullptr);
    }
}

void ModuleCloud::_on_timer_alarm()
{
    switch (_state) {
        case STATE_REGISTERING:
            // Retry timer fired
            LOG_MSG_INFO(CLOUD_LOG_EN, "retry timer — re-attempting registration");
            _attempt_registration();
            break;

        case STATE_RUNNING:
            // Heartbeat timer fired
            _pending_heartbeat = true;
            _process_events();
            // Opportunistically process one retransmission entry after heartbeat
            _retx_process_one();
            break;

        default:
            break;
    }
}

void ModuleCloud::_on_tick()
{
    _uptime_sec++;
}

// ── Private — state machine helpers ──────────────────────────────────────────

void ModuleCloud::_attempt_registration()
{
    // Get MAC from eFuse (6 raw bytes), convert to 12 hex chars
    uint8_t mac_bytes[6] = {};
    char    mac12[13]    = {};
    if (pal_efuse_get_mac(mac_bytes, sizeof(mac_bytes)) == PAL_OK) {
        snprintf(mac12, sizeof(mac12), "%02X%02X%02X%02X%02X%02X",
                 mac_bytes[0], mac_bytes[1], mac_bytes[2],
                 mac_bytes[3], mac_bytes[4], mac_bytes[5]);
    } else {
        LOG_MSG_ERROR(CLOUD_LOG_EN, "pal_efuse_get_mac failed — using empty MAC");
    }

    // root_ca: pointer cached from MsgConfigCloud (app_rootca.h static string).
    const char *root_ca = _cloud_root_ca;

    if (!_drv) {
        LOG_MSG_ERROR(CLOUD_LOG_EN, "no cloud driver set — aborting registration");
        _arm_timer(MODULE_CLOUD_RETRY_INTERVAL_MS);
        return;
    }

    LOG_MSG_INFO(CLOUD_LOG_EN, "attempting registration mac=%s", mac12);

    int32_t ret = _drv->register_device(mac12, root_ca);
    if (ret == ERROR_OK) {
        LOG_MSG_INFO(CLOUD_LOG_EN, "registration OK — state=RUNNING");
        _state = STATE_RUNNING;
        _pending_startup = true;
        _process_events();

        /* Fetch the device UUID assigned by the server and broadcast it */
        char uuid_buf[50] = {};   /* SIZE_OF_UUID = 50 in cube_sphere_config.h */
        if (_drv->get_device_uuid && _drv->get_device_uuid(uuid_buf, sizeof(uuid_buf)) == ERROR_OK) {
            LOG_MSG_INFO(CLOUD_LOG_EN, "device UUID: %s", uuid_buf);
        } else {
            LOG_MSG_WARNING(CLOUD_LOG_EN, "get_device_uuid not available or failed");
        }
        _publish_cloud_status(CLOUD_STATUS_REGISTERED, 0, uuid_buf);

        // Arm the heartbeat timer
        if (_hb_enabled) {
            _arm_timer(_hb_interval_ms);
        }
    } else {
        LOG_MSG_ERROR(CLOUD_LOG_EN, "registration FAILED ret=%d — retry in %lus",
                      ret, (unsigned long)(_hb_interval_ms / 1000));
        _publish_cloud_status(CLOUD_STATUS_REGISTER_FAILED);
        _arm_timer(MODULE_CLOUD_RETRY_INTERVAL_MS);
    }
}

void ModuleCloud::_process_events()
{
    if (_state != STATE_RUNNING) return;

    if (_pending_startup) {
        _pending_startup = false;
        LOG_MSG_INFO(CLOUD_LOG_EN, "sending startup event");
        _drv->send_startup(_build_startup_info());
    }

    if (_pending_reconnect) {
        _pending_reconnect = false;
        LOG_MSG_INFO(CLOUD_LOG_EN, "sending reconnect event");
        cloud_reconnect_info_t r = {};
        strncpy(r.ssid,       _wifi_ssid,    sizeof(r.ssid)       - 1);
        strncpy(r.password,   _wifi_password,sizeof(r.password)   - 1);
        strncpy(r.ip_address, _wifi_ip,      sizeof(r.ip_address) - 1);
        r.rssi       = _wifi_rssi;
        r.uptime_sec = _uptime_sec;
        _drv->send_reconnect(r);
    }

    if (_pending_status_update) {
        _pending_status_update = false;
        LOG_MSG_INFO(CLOUD_LOG_EN, "sending status-updated event");
        _drv->send_status_updated(_build_startup_info());
    }

    if (_pending_heartbeat && _hb_enabled) {
        _pending_heartbeat = false;
        LOG_MSG_INFO(CLOUD_LOG_EN, "sending heartbeat");
        int32_t ret = _drv->send_heartbeat(_build_hb_info());
        if (ret == ERROR_OK) {
            _publish_cloud_status(CLOUD_STATUS_HB_SENT);
        } else {
            LOG_MSG_WARNING(CLOUD_LOG_EN, "heartbeat failed ret=%d", ret);
            _publish_cloud_status(CLOUD_STATUS_HB_FAILED);
        }
        // Re-arm the heartbeat timer for the next interval
        _arm_timer(_hb_interval_ms);
    }
}

// ── Private — utilities ────────────────────────────────────────────────────────

void ModuleCloud::_arm_timer(uint32_t duration_ms)
{
    MsgTimerStart::Payload p{};
    p.source_module_id = id();
    p.start_offset_ms  = 0;
    p.duration_ms      = duration_ms;
    p.is_repetitive    = false;
    p.forced           = true;   // cancel any existing slot for this module

    auto *msg = create_typed<MsgTimerStart>(p);
    if (msg) publish(msg);
}

cloud_startup_info_t ModuleCloud::_build_startup_info() const
{
    cloud_startup_info_t s = {};
    strncpy(s.ssid,           _wifi_ssid,    sizeof(s.ssid)          - 1);
    strncpy(s.password,       _wifi_password,sizeof(s.password)      - 1);
    strncpy(s.ip_address,     _wifi_ip,      sizeof(s.ip_address)    - 1);
    strncpy(s.mac_address_str,_wifi_mac,     sizeof(s.mac_address_str)-1);
    strncpy(s.device_type,    "ferp-com",    sizeof(s.device_type)   - 1);
    strncpy(s.fw_version,     "1.0.0",       sizeof(s.fw_version)    - 1);  // TODO: from version header
    strncpy(s.hw_version,     "2602",        sizeof(s.hw_version)    - 1);
    strncpy(s.board_version,  "2602",        sizeof(s.board_version) - 1);
    strncpy(s.sd_card_status, "unknown",     sizeof(s.sd_card_status)- 1);
    s.rssi                  = _wifi_rssi;
    s.uptime_sec            = _uptime_sec;
    s.event_count_success   = _pumped_success;
    s.event_count_failure   = _pumped_failure;
    return s;
}

cloud_hb_info_t ModuleCloud::_build_hb_info() const
{
    cloud_hb_info_t hb = {};
    hb.rssi                 = _wifi_rssi;
    hb.uptime_sec           = _uptime_sec;
    hb.event_count_success  = _pumped_success;
    hb.event_count_failure  = _pumped_failure;
    return hb;
}

void ModuleCloud::_publish_cloud_status(cloud_status_event_t ev, uint8_t nozzle_idx,
                                         const char *uuid)
{
    MsgCloudStatus::Payload p{};
    p.event      = ev;
    p.nozzle_idx = nozzle_idx;
    if (uuid) {
        strncpy(p.device_uuid, uuid, sizeof(p.device_uuid) - 1);
    }
    auto *msg = create_typed<MsgCloudStatus>(p);
    if (msg) publish(msg);
}

// ── Retransmission helpers ────────────────────────────────────────────────────

void ModuleCloud::_on_sd_ready()
{
    LOG_MSG_INFO(CLOUD_LOG_EN, "SD ready — initialising retransmission manager");
    _retx_init();
}

void ModuleCloud::_retx_init()
{
    if (_retx_ready) return;
    if (!_storage) {
        LOG_MSG_WARNING(CLOUD_LOG_EN, "retx: no storage interface set — retransmission disabled");
        return;
    }

    retx_manager_config_t cfg = {};
    cfg.list_config.storage           = const_cast<storage_interface_t *>(_storage);
    cfg.list_config.parent_path       = "/sd/retx";
    cfg.list_config.file_prefix       = "evt";
    cfg.list_config.max_lines_per_file = 500;
    cfg.list_config.max_tracked_days  = 7;
    cfg.send_callback                 = [](retx_event_type_t type,
                                           const char       *payload,
                                           size_t            len,
                                           void             *user_data) -> int32_t {
        auto *self = static_cast<ModuleCloud *>(user_data);
        if (!self->_drv || self->_state != STATE_RUNNING) { return -1; }
        // Currently only PUMPED type is stored — deserialise the JSON and re-send
        // via the driver's raw pumped path.  If the driver gains a send_raw_pumped
        // entry point this becomes trivial; for now use send_pumped with zeroed
        // fields that the driver will fill from the JSON payload.
        // TODO: extend cloud_driver_t with send_raw_pumped(json, len) for faithful replay.
        (void)type; (void)payload; (void)len;
        return -1;   // not yet wired to driver — prevents ack, keeps in queue
    };
    cfg.user_data                     = this;
    cfg.retry_interval_ms             = 60000;   // 1 minute between retransmit attempts

    int32_t ret = retx_mgr_init(&_retx_mgr, &cfg);
    if (ret == RETX_MGR_OK) {
        _retx_ready = true;
        retx_mgr_cleanup(&_retx_mgr);   // delete files older than 7 days at startup
        LOG_MSG_INFO(CLOUD_LOG_EN, "retx: manager ready");
    } else {
        LOG_MSG_ERROR(CLOUD_LOG_EN, "retx: init failed (%ld)", (long)ret);
    }
}

void ModuleCloud::_retx_store_pumped(const MsgFuelPumped::Payload &p, const char * /*json_payload*/)
{
    if (!_retx_ready) {
        LOG_MSG_WARNING(CLOUD_LOG_EN, "retx: not ready — failed event lost (SD not mounted?)");
        return;
    }

    // Build a compact JSON payload to persist
    char json[256];
    snprintf(json, sizeof(json),
             "{\"nozzle\":%u,\"vol\":%lu,\"unit\":%lu,\"total\":%lu,\"ts\":%llu}",
             (unsigned)p.nozzle_idx,
             (unsigned long)p.vol_lx1000,
             (unsigned long)p.unit_pricex100,
             (unsigned long)p.total_pricex100,
             (unsigned long long)time(nullptr));

    // Date key from wall-clock time
    char date_key[16] = "00000000";
    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    strftime(date_key, sizeof(date_key), "%Y%m%d", &tm_info);

    retx_mgr_add_failed_event(&_retx_mgr, date_key, RETX_EVENT_TYPE_PUMPED,
                               json, strlen(json));
}

void ModuleCloud::_retx_process_one()
{
    if (!_retx_ready || _state != STATE_RUNNING) return;
    retx_mgr_process(&_retx_mgr);
}
