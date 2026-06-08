// module_cubesphere.cpp
//
// ModuleCubeSphere — HTTPS cloud manager.
// All CubeSphere HTTP session logic is inlined here (previously in cube_sphere_api.cpp).

#include "module_cubesphere.h"
#include "msg_config_ready.h"
#include "msg_config_get_cloud.h"
#include "msg_config_get_wifi.h"
#include "msg_config_cloud.h"
#include "msg_config_wifi.h"
#include "msg_wifi_event.h"
#include "msg_internet_status.h"
#include "msg_cubesphere_status.h"
#include "msg_fuel_pumped.h"
#include "msg_nozzle_state.h"
#include "msg_fuel_print_status.h"
#include "msg_timer_start.h"
#include "msg_timer_alarm.h"
#include "msg_sd_ready.h"
#include "msg_system_status.h"
#include "list_manager.h"

// HTTP messages (DIRECT to/from ModuleHttp)
#include "msg_http_request.h"
#include "msg_http_result.h"
#include "msg_http_response_header.h"
#include "msg_dev_info_write.h"
#include "app_device_info.h"

#include "pal_logger.h"
#include "pal_efuse.h"
#include "pal_time.h"
#include "pal_crypto.h"

#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#define __TAG__    "CUBESPH "
#define CSP_LOG_EN  true

// Secret key for SAS-AC1 token computation
const char ModuleCubeSphere::_cs_key[] = "y4M5oJVfjAWeN059p";

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleCubeSphere s_instance;
ModuleCubeSphere *ModuleCubeSphere::instance() { return &s_instance; }

// CubeSphere cloud endpoints
#define CS_URL_BOOTSTRAP   "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/bootstrap/core/v1/device"
#define CS_URL_DEV_CONFIG  "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/ingress/core/v1/device/config"
#define CS_URL_EVENTS      "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/ingress/core/v1/device/event"

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleCubeSphere::init()
{
    // Notifications
    subscribe(MsgConfigReady::ID);
    subscribe(MsgConfigCloud::ID);
    subscribe(MsgConfigWifi::ID);
    subscribe(MsgWifiEvent::ID);
    subscribe(MsgInternetStatus::ID);
    subscribe(MsgFuelPumped::ID);
    subscribe(MsgNozzleState::ID);         // pump-start
    subscribe(MsgFuelPrintStatus::ID);         // printok
    subscribe(MsgTimerAlarm::ID);
    subscribe(MsgSdReady::ID);
    subscribe(MSG_ID_TICK_1000MS);
    subscribe(MSG_ID_SYSTEM_STATUS);

    // DIRECT responses from ModuleHttp
    subscribe(MSG_ID_HTTP_RESPONSE_HEADER);
    subscribe(MSG_ID_HTTP_RESULT);

    hsys_queue_init(&_pump_q,        k_pump_q_size,        sizeof(PumpedQEntry));
    hsys_queue_init(&_print_ok_q,   k_print_ok_q_size,   sizeof(PrintOkQEntry));

    LOG_MSG_INFO(CSP_LOG_EN, "init — state=WAIT_FOR_INTERNET");
}

// ── Message dispatcher ────────────────────────────────────────────────────────

void ModuleCubeSphere::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        // Notifications
        case MsgConfigReady::ID:           _on_config_ready();           break;
        case MsgConfigCloud::ID:           _on_config_cloud(msg);        break;
        case MsgConfigWifi::ID:            _on_config_wifi(msg);         break;
        case MsgWifiEvent::ID:             _on_wifi_event(msg);          break;
        case MsgInternetStatus::ID:        _on_internet_status(msg);     break;
        case MsgFuelPumped::ID:            _on_fuel_pumped(msg);         break;
        case MsgNozzleState::ID:           _on_nozzle_state(msg);        break;
        case MsgFuelPrintStatus::ID:       _on_fuel_print_ok(msg);       break;
        case MsgSdReady::ID:               _on_sd_ready();               break;
        case MsgTimerAlarm::ID:            _on_timer_alarm();            break;
        case MSG_ID_TICK_1000MS:           _on_tick();                   break;
        case MSG_ID_SYSTEM_STATUS:         _on_system_status(msg);       break;
        // HTTP session responses (DIRECT from ModuleHttp)
        case MSG_ID_HTTP_RESPONSE_HEADER:  _on_http_response_header(msg);break;
        case MSG_ID_HTTP_RESULT:           _on_http_result(msg);         break;
        default: break;
    }
}

// ── Message handlers ──────────────────────────────────────────────────────────

void ModuleCubeSphere::_on_config_ready()
{
    MsgConfigGetCloud::Payload req{};
    req.source_module_id = id();
    auto *msg = create_typed<MsgConfigGetCloud>(req);
    if (msg) publish(msg);

    MsgConfigGetWifi::Payload wreq{};
    wreq.source_module_id = id();
    auto *wmsg = create_typed<MsgConfigGetWifi>(wreq);
    if (wmsg) publish(wmsg);
}

void ModuleCubeSphere::_on_config_cloud(const hsys_msg_t &msg)
{
    auto p = MsgConfigCloud::deserialize(msg);
    // _cloud_root_ca = p.root_ca;
    if (p.hb_interval_s > 0) _hb_interval_ms = p.hb_interval_s * 1000UL;
    _hb_enabled         = p.hb_enabled;
    _cloud_config_ready = true;

    // LOG_MSG_INFO(CSP_LOG_EN, "cloud config: root_ca=%s hb_enabled=%d interval=%us",
    //              _cloud_root_ca ? "***" : "(null)", (int)p.hb_enabled, (unsigned)p.hb_interval_s);
    LOG_MSG_INFO(CSP_LOG_EN, "cloud config: hb_enabled=%d interval=%us",
                 (int)p.hb_enabled, (unsigned)p.hb_interval_s);

    // If WiFi already has an IP (GOT_IP arrived before config), start registration now
    if (_wifi_ip[0] != '\0' && _state == STATE_WAIT_FOR_INTERNET) {
        LOG_MSG_INFO(CSP_LOG_EN, "cloud config received after GOT_IP — starting registration");
        _state = STATE_REGISTERING;
        _start_registration();
    }
}

void ModuleCubeSphere::_on_config_wifi(const hsys_msg_t &msg)
{
    auto p = MsgConfigWifi::deserialize(msg);
    strncpy(_wifi_ssid,     p.ssid,     sizeof(_wifi_ssid)     - 1);
    strncpy(_wifi_password, p.password, sizeof(_wifi_password) - 1);
    LOG_MSG_INFO(CSP_LOG_EN, "wifi config: ssid=\"%s\"", _wifi_ssid);
}

void ModuleCubeSphere::_on_wifi_event(const hsys_msg_t &msg)
{
    auto p = MsgWifiEvent::deserialize(msg);
    switch (p.event) {
        case WIFI_EVENT_STA_GOT_IP:
            if (_wifi_was_connected) {
                _wifi_reconnected = true;
                if (_state == STATE_RUNNING) _pending_reconnect = true;
            }
            _wifi_was_connected = true;
            _wifi_rssi = p.rssi;
            strncpy(_wifi_ssid, p.ssid,        sizeof(_wifi_ssid) - 1);
            strncpy(_wifi_ip,   p.ip_address,  sizeof(_wifi_ip)   - 1);
            strncpy(_wifi_mac,  p.mac_address, sizeof(_wifi_mac)  - 1);
            LOG_MSG_INFO(CSP_LOG_EN, "WiFi GOT_IP ssid=%s ip=%s rssi=%d",
                         _wifi_ssid, _wifi_ip, _wifi_rssi);

            // Trigger registration if cloud config is ready and not yet registered
            if (_cloud_config_ready && _state == STATE_WAIT_FOR_INTERNET) {
                LOG_MSG_INFO(CSP_LOG_EN, "GOT_IP with cloud config ready — starting registration");
                _state = STATE_REGISTERING;
                _start_registration();
            }
            break;
        case WIFI_EVENT_STA_RSSI_CHANGED:
            _wifi_rssi = p.rssi;
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            _wifi_was_connected = true;
            LOG_MSG_INFO(CSP_LOG_EN, "WiFi disconnected");
            break;
        default: break;
    }
}

void ModuleCubeSphere::_on_internet_status(const hsys_msg_t &msg)
{
    auto p = MsgInternetStatus::deserialize(msg);
    _internet_connected = p.connected;
    LOG_MSG_INFO(CSP_LOG_EN, "internet %s", p.connected ? "UP" : "DOWN");
    // Internet status is informational — registration is triggered by WiFi GOT_IP
}

void ModuleCubeSphere::_on_fuel_pumped(const hsys_msg_t &msg)
{
    auto p = MsgFuelPumped::deserialize(msg);

    if (_state != STATE_RUNNING) {
        if (p.nozzle_idx >= CS_NO_NOZZLES) {
            LOG_MSG_ERROR(CSP_LOG_EN, "invalid nozzle_idx %u pre-registration — discarding", p.nozzle_idx);
            return;
        }
        LOG_MSG_WARNING(CSP_LOG_EN, "fuel pumped but not RUNNING — storing for retx");
        _pumped_failure++;
        _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED, p.nozzle_idx);
        _retx_store_pumped(p);
        return;
    }

    if (p.nozzle_idx >= CS_NO_NOZZLES) {
        LOG_MSG_ERROR(CSP_LOG_EN, "invalid nozzle_idx %u (max=%u) — discarding", p.nozzle_idx, (unsigned)CS_NO_NOZZLES);
        _pumped_failure++;
        _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED, p.nozzle_idx);
        return;
    }
    if (_cs_nozzles[p.nozzle_idx].uuid[0] == '\0') {
        // Nozzle index is in range but not yet provisioned in CubeSphere.
        // Retx would immediately discard it too (same check), so skip the SD write.
        LOG_MSG_WARNING(CSP_LOG_EN, "nozzle_idx %u not provisioned in cloud — discarding pump event", p.nozzle_idx);
        _pumped_failure++;
        _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED, p.nozzle_idx);
        return;
    }

    if (hsys_queue_size(&_pump_q) + 1U > k_pump_q_size) {
        LOG_MSG_WARNING(CSP_LOG_EN,
                        "pump queue full (nozzle=%u, q=%u) — storing for retx",
                        (unsigned)p.nozzle_idx, (unsigned)hsys_queue_size(&_pump_q));
        _pumped_failure++;
        _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED, p.nozzle_idx);
        _retx_store_pumped(p);
        return;
    }

    uint32_t event_id = _pumped_success + _pumped_failure + 1;

    PumpedQEntry e;
    e.nozzle_idx      = p.nozzle_idx;
    e.vol_lx1000      = p.vol_lx1000;
    e.unit_pricex100  = p.unit_pricex100;
    e.total_pricex100 = p.total_pricex100;
    e.ts_epoch        = (time_t)p.time_stamp;
    e.event_id        = event_id;
    e.ne_id           = p.ne_id;

    if(!hsys_queue_send(&_pump_q, &e, 0))
    {
        LOG_MSG_ERROR(CSP_LOG_EN, "failed to queue pumped event (nozzle=%u id=%u q=%u)",
                      (unsigned)p.nozzle_idx, (unsigned)event_id, (unsigned)hsys_queue_size(&_pump_q));
        _pumped_failure++;
        _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED, p.nozzle_idx);
        _retx_store_pumped(p);
        return;
    }

    LOG_MSG_DEBUG(CSP_LOG_EN, "pumped queued (nozzle=%u id=%u q=%u)",
                  (unsigned)p.nozzle_idx, (unsigned)event_id, (unsigned)hsys_queue_size(&_pump_q));
    _start_next_event();
}

void ModuleCubeSphere::_on_nozzle_state(const hsys_msg_t &msg)
{
    auto p = MsgNozzleState::deserialize(msg);
    if (p.state != NOZZLE_PUMPING) return;   // only pump-start is cloud-reported
    if (p.nozzle_idx >= CS_NO_NOZZLES) return;

    if (_cs_nozzles[p.nozzle_idx].uuid[0] == '\0') {
        LOG_MSG_DEBUG(CSP_LOG_EN, "nozzle[%u] PUMPING — not provisioned in cloud, skipping pump-start",
                      (unsigned)p.nozzle_idx);
        return;
    }

    if (_state != STATE_RUNNING) {
        LOG_MSG_DEBUG(CSP_LOG_EN, "nozzle[%u] PUMPING — not RUNNING, skipping pump-start",
                      (unsigned)p.nozzle_idx);
        return;
    }

    _pump_start_nozzle_idx          = p.nozzle_idx;
    _pending_pump_start[p.nozzle_idx] = true;
    LOG_MSG_DEBUG(CSP_LOG_EN, "nozzle[%u] PUMPING — pending pump-start event",
                  (unsigned)p.nozzle_idx);
    _start_next_event();
}

void ModuleCubeSphere::_on_fuel_print_ok(const hsys_msg_t &msg)
{
    auto p = MsgFuelPrintStatus::deserialize(msg);
    if (p.nozzle_idx >= CS_NO_NOZZLES) return;

    if (_state != STATE_RUNNING) {
        LOG_MSG_WARNING(CSP_LOG_EN, "print-ok nozzle[%u] — not RUNNING (state=%d), discarding",
                        (unsigned)p.nozzle_idx, (int)_state);
        return;
    }

    if (p.status != PRINT_STATUS_OK) {
        LOG_MSG_INFO(CSP_LOG_EN, "print-ok nozzle[%u] status=%d (not OK) — no cloud event",
                     (unsigned)p.nozzle_idx, (int)p.status);
        return;
    }

    PrintOkQEntry e;
    e.nozzle_idx         = p.nozzle_idx;
    e.dispenser_event_id = p.dispenser_event_id;
    e.ne_id              = p.ne_id;

    if (!hsys_queue_send(&_print_ok_q, &e, 0)) {
        LOG_MSG_WARNING(CSP_LOG_EN, "print-ok queue full (depth=%u), dropping nozzle[%u]",
                        (unsigned)hsys_queue_size(&_print_ok_q), (unsigned)p.nozzle_idx);
        return;
    }

    LOG_MSG_INFO(CSP_LOG_EN, "print-ok queued for nozzle %u (queue depth=%u)",
                 (unsigned)p.nozzle_idx, (unsigned)hsys_queue_size(&_print_ok_q));
    _start_next_event();
}

void ModuleCubeSphere::_on_timer_alarm()
{
    switch (_state) {
        case STATE_REGISTERING:
            if (_http_phase == HTTP_IDLE) 
            {
                if (_reg_step == REG_STEP_WAIT_2) 
                {
                    LOG_MSG_DEBUG(CSP_LOG_EN, "step1->step2 cooldown elapsed — starting step2");
                    _start_reg_step_2();
                } 
                else 
                {
                    LOG_MSG_INFO(CSP_LOG_EN, "retry timer — re-attempting registration");
                    _start_registration();
                }
            }
            break;
        case STATE_RUNNING:
            _pending_heartbeat = true;
            LOG_MSG_DEBUG(CSP_LOG_EN, "heartbeat timer alarm — pending heartbeat event");
            _start_next_event();
            break;
        default: break;
    }
}

void ModuleCubeSphere::_on_tick()
{
    _uptime_sec++;

    // Watchdog: if HTTP_EXECUTING persists for >45 s the HTTP result was dropped
    // (network_task inbox was full when http_task sent its reply; http_task has
    // already reset to IDLE, but we never received the message).  Retry the
    // current operation so registration / event sending is not permanently stuck.
    if (_http_phase == HTTP_EXECUTING) {
        if (++_http_exec_ticks >= 45) {
            LOG_MSG_WARNING(CSP_LOG_EN,
                            "HTTP_EXECUTING watchdog: stuck for 45s (evt=%d state=%d) — result dropped, recovering",
                            (int)_cur_evt, (int)_state);
            _http_exec_ticks = 0;
            _http_phase = HTTP_IDLE;
            if (_state == STATE_REGISTERING) {
                switch (_reg_step) {
                    case REG_STEP_1:      _start_reg_step_1(); break;
                    case REG_STEP_WAIT_2:
                    case REG_STEP_2:      _start_reg_step_2(); break;
                    case REG_STEP_3:      _start_reg_step_3(); break;
                }
            } else if (_state == STATE_RUNNING) {
                _recover_exec_miss();
            }
        }
    } else {
        _http_exec_ticks = 0;
    }

    // Retransmit check every 60 seconds when running
    if (_state == STATE_RUNNING && _retx_ready) {
        if (_retx_check_countdown > 0) {
            _retx_check_countdown--;
        } else {
            _retx_check_countdown = 60;
            _retx_last_send_failed = false;   // reset failure flag for next round
            _retx_try_send_one();
        }
    }
}

void ModuleCubeSphere::_on_system_status(const hsys_msg_t &msg)
{
    auto p = MsgSystemStatus::deserialize(msg);
    bool was_idle    = _system_is_idle;
    _system_is_idle = (p.status == SYSTEM_STATUS_IDLE);

    LOG_MSG_INFO(CSP_LOG_EN, "system_status -> %s",
                 _system_is_idle ? "IDLE" : "BUSY");

    // When system becomes idle, try to send a queued retransmit immediately
    // rather than waiting for the next 60-second countdown.
    if (!was_idle && _system_is_idle && _retx_ready && _internet_connected
        && _state == STATE_RUNNING && !_retx_in_progress && !_retx_last_send_failed)
    {
        _retx_try_send_one();
    }
}

// ── HTTP response handlers ────────────────────────────────────────────────────

void ModuleCubeSphere::_on_http_response_header(const hsys_msg_t &msg)
{
    // Only relevant during STEP_1 — capture nonce from www-authenticate
    if (_state != STATE_REGISTERING || _reg_step != REG_STEP_1) return;

    const char *key = MsgHttpResponseHeader::get_key(msg);
    const char *val = MsgHttpResponseHeader::get_value(msg);
    if (!key || !val) return;
    if (strcasecmp(key, "www-authenticate") != 0) return;

    const char *ns = strstr(val, "nonce=\"");
    if (!ns) return;
    ns += 7;
    const char *ne = strchr(ns, '"');
    if (!ne || (size_t)(ne - ns) >= sizeof(_reg_nonce)) return;

    memset(_reg_nonce, 0, sizeof(_reg_nonce));
    memcpy(_reg_nonce, ns, (size_t)(ne - ns));
    LOG_MSG_DEBUG(CSP_LOG_EN, "captured nonce (len=%u)", (unsigned)(ne - ns));
}

void ModuleCubeSphere::_recover_exec_miss()
{
    switch (_cur_evt) {
        case EVT_STARTUP:       _pending_startup       = true; break;
        case EVT_RECONNECT:     _pending_reconnect     = true; break;
        case EVT_STATUS_UPDATE: _pending_status_update = true; break;
        case EVT_PUMP_START:
            // Restore the pending flag so the pump-start is retried.
            // Only restore if the nozzle is still provisioned — _on_nozzle_state now
            // guards this at ingress, so a miss on an unconfigured nozzle can't happen,
            // but be defensive in case the config changes between sessions.
            if (_pump_start_nozzle_idx < CS_NO_NOZZLES &&
                _cs_nozzles[_pump_start_nozzle_idx].uuid[0] != '\0') {
                _pending_pump_start[_pump_start_nozzle_idx] = true;
            }
            break;
        case EVT_PRINT_OK: {
            PrintOkQEntry e;
            e.nozzle_idx         = _print_ok_nozzle_idx;
            e.dispenser_event_id = _print_ok_dispenser_event_id;
            e.ne_id              = _print_ok_ne_id;
            hsys_queue_send(&_print_ok_q, &e, 0);
            break;
        }
        case EVT_PUMPED: {
            // Entry was already dequeued from _pump_q into _last_pumped_* — re-enqueue it.
            PumpedQEntry e;
            e.nozzle_idx      = _last_pumped_nozzle_idx;
            e.vol_lx1000      = _last_pumped_vol_lx1000;
            e.unit_pricex100  = _last_pumped_unit_px100;
            e.total_pricex100 = _last_pumped_total_px100;
            e.ts_epoch        = _last_pumped_ts_epoch;
            e.ne_id           = _last_pumped_ne_id;
            e.event_id        = _last_pumped_event_id;
            if (!hsys_queue_send(&_pump_q, &e, 0))
                LOG_MSG_ERROR(CSP_LOG_EN, "start-miss recovery: failed to re-queue pumped event — data lost");
            break;
        }
        case EVT_PUMPED_RETX:
            // Release the in-progress lock so the retx cycle can retry this entry.
            // The list item is NOT acked — it stays in place for the next retx attempt.
            _retx_in_progress = false;
            break;
        default: break;  // EVT_HEARTBEAT: best-effort, drop silently
    }

    _cur_evt = EVT_NONE;
    _arm_timer(2000);
}

void ModuleCubeSphere::_on_http_result(const hsys_msg_t &msg)
{
    if (_http_phase != HTTP_EXECUTING) return;

    auto fcheck = MsgHttpResult::get_fields(msg);
    if (fcheck.result == HTTP_RESULT_BUSY) {
        LOG_MSG_WARNING(CSP_LOG_EN,
                        "HTTP_RESULT_BUSY (evt=%d state=%d) — http_task EXECUTING, retry in 2s",
                        (int)_cur_evt, (int)_state);
        _http_exec_ticks = 0;
        _http_phase = HTTP_IDLE;
        if (_state == STATE_RUNNING) {
            _recover_exec_miss();  // re-queues current event and arms 2s timer
        } else {
            _arm_timer(2000);      // registration will restart from current step on alarm
        }
        return;
    }

    _http_exec_ticks = 0;
    _http_phase = HTTP_IDLE;

    if (_state == STATE_REGISTERING) {
        switch (_reg_step) {
            case REG_STEP_1:      _on_reg_step1_result(msg); break;
            case REG_STEP_2:      _on_reg_step2_result(msg); break;
            case REG_STEP_3:      _on_reg_step3_result(msg); break;
            case REG_STEP_WAIT_2: /* should not reach here — no HTTP session open in WAIT_2 */ break;
        }
    } else if (_state == STATE_RUNNING) {
        _on_event_result(msg);
    }
}

// ── Registration helpers ──────────────────────────────────────────────────────

void ModuleCubeSphere::_start_registration()
{
    memset(_reg_nonce,   0, sizeof(_reg_nonce));
    memset(&_cs_net_cfg, 0, sizeof(_cs_net_cfg));
    memset(_cs_nozzles,  0, sizeof(_cs_nozzles));
    _reg_step = REG_STEP_1;
    _start_reg_step_1();
}

void ModuleCubeSphere::_start_reg_step_1()
{
    LOG_MSG_INFO(CSP_LOG_EN, "reg step1 — GET /bootstrap (expecting 401 + nonce)");
    _begin_http_request(PAL_HTTP_METHOD_GET, 30000,
                        CS_URL_BOOTSTRAP, nullptr, nullptr, 0, "www-authenticate");
}

void ModuleCubeSphere::_start_reg_step_2()
{
    uint8_t mac_bytes[6] = {};
    char    mac12[13]    = {};
    if (pal_efuse_get_mac(mac_bytes, sizeof(mac_bytes)) == PAL_OK) {
        snprintf(mac12, sizeof(mac12), "%02X%02X%02X%02X%02X%02X",
                 mac_bytes[0], mac_bytes[1], mac_bytes[2],
                 mac_bytes[3], mac_bytes[4], mac_bytes[5]);
    }

    char token[PAL_SHA256_DIGEST_LENGTH * 2 + 1] = {};
    _cs_calc_sha256(_reg_nonce, mac12, _cs_key, token, sizeof(token));
    snprintf(_auth_hdr, sizeof(_auth_hdr),
             "SAS-AC1 nonce=\"%s\" id=\"%s\" token=\"%s\"",
             _reg_nonce, mac12, token);

    LOG_MSG_INFO(CSP_LOG_EN, "reg step2 — GET /bootstrap with SAS-AC1 auth");
    _reg_step = REG_STEP_2;
    _begin_http_request(PAL_HTTP_METHOD_GET, 30000,
                        CS_URL_BOOTSTRAP, _auth_hdr, nullptr, 0, nullptr);
}

void ModuleCubeSphere::_start_reg_step_3()
{
    snprintf(_auth_hdr, sizeof(_auth_hdr),
             "Basic %s", _cs_net_cfg.basic_authentication_base64);
    LOG_MSG_INFO(CSP_LOG_EN, "reg step3 — GET /device/config with Basic auth");
    _reg_step = REG_STEP_3;
    _begin_http_request(PAL_HTTP_METHOD_GET, 30000,
                        CS_URL_DEV_CONFIG, _auth_hdr, nullptr, 0, nullptr);
}

void ModuleCubeSphere::_on_reg_step1_result(const hsys_msg_t &msg)
{
    auto f = MsgHttpResult::get_fields(msg);

    if (f.result != HTTP_RESULT_SUCCESS || f.status_code != 401) {
        LOG_MSG_ERROR(CSP_LOG_EN, "reg step1: result=%d status=%d (expected 401)",
                      (int)f.result, (int)f.status_code);
        _reg_failed();
        return;
    }
    if (_reg_nonce[0] == '\0') {
        LOG_MSG_ERROR(CSP_LOG_EN, "reg step1: no nonce captured from www-authenticate");
        _reg_failed();
        return;
    }
    LOG_MSG_INFO(CSP_LOG_EN, "reg step1 OK — nonce captured, waiting 1s before step2");
    _reg_step = REG_STEP_WAIT_2;
    _arm_timer(1000);   // give GCLB time to fully tear down the step1 TLS session
}

void ModuleCubeSphere::_on_reg_step2_result(const hsys_msg_t &msg)
{
    auto f = MsgHttpResult::get_fields(msg);

    if (f.result != HTTP_RESULT_SUCCESS ||
        (f.status_code != 200 && f.status_code != 201)) {
        LOG_MSG_ERROR(CSP_LOG_EN, "reg step2: result=%d status=%d",
                      (int)f.result, (int)f.status_code);
        _reg_failed();
        return;
    }
    if (!f.body || f.body_len == 0) {
        LOG_MSG_ERROR(CSP_LOG_EN, "reg step2: empty body");
        _reg_failed();
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, (const char *)f.body, DeserializationOption::NestingLimit(20));

    const char *dev_id = nullptr;
    const char *secret = nullptr;
    if (doc["data"].is<JsonObject>()) {
        JsonObject data = doc["data"].as<JsonObject>();
        dev_id = data["device_id"] | (const char *)nullptr;
        secret = data["secret"]    | (const char *)nullptr;
    }
    if (!dev_id || !secret) {
        LOG_MSG_ERROR(CSP_LOG_EN, "reg step2: missing device_id or secret");
        _reg_failed();
        return;
    }

    char id_secret[CS_SIZE_UUID + CS_SIZE_SECRET + 2] = {};
    snprintf(id_secret, sizeof(id_secret), "%s:%s", dev_id, secret);
    char b64[CS_SIZE_SECRET] = {};
    pal_crypto_base64_encode((const uint8_t *)id_secret, strlen(id_secret),
                              b64, sizeof(b64));

    strncpy(_cs_net_cfg.agent_uuid,                  dev_id, CS_SIZE_UUID   - 1);
    strncpy(_cs_net_cfg.basic_authentication_base64, b64,    CS_SIZE_SECRET - 1);
    LOG_MSG_DEBUG(CSP_LOG_EN, "reg step2 OK — device_id=%s", _cs_net_cfg.agent_uuid);

    // Push the cloud-assigned UUID into DeviceInfo so the rest of the system
    // (ModuleMqtt UUID topics, DevInfo bar, etc.) sees the provisioned identity.
    {
        hsys_msg_t *w = MsgDevInfoWrite::create_str(
            id(), DEV_INFO_KEY_DEVICE_UUID, _cs_net_cfg.agent_uuid);
        if (w) publish(w);
    }

    _start_reg_step_3();
}

void ModuleCubeSphere::_on_reg_step3_result(const hsys_msg_t &msg)
{
    auto f = MsgHttpResult::get_fields(msg);

    if (f.result != HTTP_RESULT_SUCCESS ||
        (f.status_code != 200 && f.status_code != 201)) {
        LOG_MSG_ERROR(CSP_LOG_EN, "reg step3: result=%d status=%d",
                      (int)f.result, (int)f.status_code);
        _reg_failed();
        return;
    }
    if (!f.body || f.body_len == 0) {
        LOG_MSG_ERROR(CSP_LOG_EN, "reg step3: empty body");
        _reg_failed();
        return;
    }

    JsonDocument doc;
    deserializeJson(doc, (const char *)f.body, DeserializationOption::NestingLimit(20));

    LOG_MSG_DEBUG(CSP_LOG_EN, "reg step3 response body: %s", (const char *)f.body);

    bool nozzles_ok = false;
    if (doc["data"].is<JsonObject>()) {
        JsonObject data    = doc["data"].as<JsonObject>();
        JsonArray  nozzles = data["nozzles"].as<JsonArray>();
        int        n       = (int)nozzles.size();
        if (n > CS_NO_NOZZLES) n = CS_NO_NOZZLES;
        nozzles_ok = (n > 0);
        memset(_cs_nozzles, 0, sizeof(_cs_nozzles));
        for (int i = 0; i < n; i++) {
            JsonObject nz     = nozzles[i].as<JsonObject>();
            const char *uuid  = nz["device_id"]    | (const char *)nullptr;
            const char *ft    = nz["fuel_type"]     | (const char *)nullptr;
            const char *ft_s  = nz["fuel_type_str"] | (const char *)nullptr;
            const char *nz_id = nz["id"]            | (const char *)nullptr;
            if (!uuid || !ft || !ft_s || !nz_id) { nozzles_ok = false; break; }
            strncpy(_cs_nozzles[i].uuid,          uuid,  CS_SIZE_UUID          - 1);
            strncpy(_cs_nozzles[i].fuel_type,     ft,    CS_SIZE_FUEL_TYPE     - 1);
            strncpy(_cs_nozzles[i].fuel_type_str, ft_s,  CS_SIZE_FUEL_TYPE_STR - 1);
            strncpy(_cs_nozzles[i].nozzle_id,     nz_id, CS_SIZE_NOZZLE_ID    - 1);
            LOG_MSG_DEBUG(CSP_LOG_EN, "nozzle[%d] uuid=%s ft=%s",
                          i, _cs_nozzles[i].uuid, _cs_nozzles[i].fuel_type);
        }
    }
    if (!nozzles_ok) {
        LOG_MSG_ERROR(CSP_LOG_EN, "reg step3: nozzle config incomplete");
        _reg_failed();
        return;
    }

    LOG_MSG_INFO(CSP_LOG_EN, "registration complete — state=RUNNING");
    _state = STATE_RUNNING;
    _publish_status(CUBESPHERE_STATUS_REGISTERED, 0, _cs_net_cfg.agent_uuid);
    _pending_startup = true;
    _start_next_event();
}

void ModuleCubeSphere::_reg_failed()
{
    LOG_MSG_ERROR(CSP_LOG_EN, "registration failed — retry in %lus",
                  (unsigned long)(MODULE_CUBESPHERE_RETRY_INTERVAL_MS / 1000));
    _publish_status(CUBESPHERE_STATUS_REGISTER_FAILED);
    _arm_timer(MODULE_CUBESPHERE_RETRY_INTERVAL_MS);
}

// ── Running event helpers ─────────────────────────────────────────────────────

void ModuleCubeSphere::_start_next_event()
{
    if (_state != STATE_RUNNING || _http_phase != HTTP_IDLE) return;

    if (_pending_startup) 
    {
        _pending_startup = false;
        _cur_evt = EVT_STARTUP;
        LOG_MSG_DEBUG(CSP_LOG_EN, "pending startup event");
    } 
    else if (_pending_reconnect) 
    {
        _pending_reconnect = false;
        _cur_evt = EVT_RECONNECT;
        LOG_MSG_DEBUG(CSP_LOG_EN, "pending reconnect event");
    } 
    else if (_pending_status_update) 
    {
        _pending_status_update = false;
        _cur_evt = EVT_STATUS_UPDATE;
        LOG_MSG_DEBUG(CSP_LOG_EN, "pending status update event");
    } 
    else if (_pending_pump_start[0] || _pending_pump_start[1]) 
    {
        // Find first pending pump-start nozzle
        for (int i = 0; i < CS_NO_NOZZLES; i++) 
        {
            if (_pending_pump_start[i]) {
                _pending_pump_start[i] = false;
                _pump_start_nozzle_idx = (uint8_t)i;
                break;
            }
        }
        _cur_evt = EVT_PUMP_START;
        LOG_MSG_DEBUG(CSP_LOG_EN, "pending pump-start event for nozzle %u", (unsigned)_pump_start_nozzle_idx);
    } 
    else if (!hsys_queue_is_empty(&_pump_q)) 
    {
        // Dequeue the next pump-end entry and load its data into the _last_pumped_*
        // fields used by _build_event_json().
        PumpedQEntry e;
        hsys_queue_receive(&_pump_q, &e, 0);
        _last_pumped_nozzle_idx  = e.nozzle_idx;
        _last_pumped_vol_lx1000  = e.vol_lx1000;
        _last_pumped_unit_px100  = e.unit_pricex100;
        _last_pumped_total_px100 = e.total_pricex100;
        _last_pumped_ts_epoch    = e.ts_epoch;
        _last_pumped_ne_id       = e.ne_id;
        _last_pumped_event_id    = e.event_id;
        _cur_evt = EVT_PUMPED;

        LOG_MSG_DEBUG(CSP_LOG_EN, "pending pumped event for nozzle %u (vol=%u lx1000, unit_px=%u x100, total_px=%u x100)",
                    (unsigned)e.nozzle_idx, (unsigned)e.vol_lx1000,
                    (unsigned)e.unit_pricex100, (unsigned)e.total_pricex100);
    } 
    else if (!hsys_queue_is_empty(&_print_ok_q))
    {
        PrintOkQEntry e;
        hsys_queue_receive(&_print_ok_q, &e, 0);
        _print_ok_nozzle_idx         = e.nozzle_idx;
        _print_ok_dispenser_event_id = e.dispenser_event_id;
        _print_ok_ne_id              = e.ne_id;
        _cur_evt = EVT_PRINT_OK;
        LOG_MSG_DEBUG(CSP_LOG_EN, "pending print-ok event for nozzle %u", (unsigned)e.nozzle_idx);
    }
    else if (_pending_heartbeat && _hb_enabled) 
    {
        _pending_heartbeat = false;
        _cur_evt = EVT_HEARTBEAT;
        LOG_MSG_DEBUG(CSP_LOG_EN, "pending heartbeat event");
    } 
    else 
    {
        if (_hb_enabled) _arm_timer(_hb_interval_ms);
        return;
    }

    if (!_build_event_json(_cur_evt)) 
    {
        LOG_MSG_ERROR(CSP_LOG_EN, "failed to build JSON for event %d", (int)_cur_evt);
        _cur_evt = EVT_NONE;
        _start_next_event();
        return;
    }

    {
        char auth[512] = {};
        snprintf(auth, sizeof(auth), "Basic %s", _cs_net_cfg.basic_authentication_base64);
        LOG_MSG_INFO(CSP_LOG_EN, "starting HTTP POST for event %d", (int)_cur_evt);
        _begin_http_request(PAL_HTTP_METHOD_POST, 10000,
                            CS_URL_EVENTS, auth,
                            _event_json, (uint32_t)strlen(_event_json), nullptr);
    }
}

bool ModuleCubeSphere::_build_event_json(evt_type_t evt)
{
    struct timeval now_tv;
    gettimeofday(&now_tv, nullptr);
    char ts[64] = {};
    _cs_format_iso8601(now_tv.tv_sec + (int)(3600 * 5.5), "+05:30", ts, sizeof(ts));

    JsonDocument doc;
    JsonObject root   = doc.to<JsonObject>();
    JsonArray  events = root["events"].to<JsonArray>();

    switch (evt) {
        case EVT_HEARTBEAT: {
            JsonObject e0 = events.add<JsonObject>();
            e0["device"] = _cs_net_cfg.agent_uuid;
            e0["time"]   = ts;
            e0["event"]  = "core/heartbeat";
            JsonObject b0 = e0["body"].to<JsonObject>();
            b0["rssi"]   = _wifi_rssi;
            b0["uptime"] = _uptime_sec;
            for (int i = 0; i < CS_NO_NOZZLES; i++) {
                if (_cs_nozzles[i].uuid[0] == '\0') continue;
                JsonObject en = events.add<JsonObject>();
                en["device"] = _cs_nozzles[i].uuid;
                en["time"]   = ts;
                en["event"]  = "core/heartbeat";
                JsonObject bn = en["body"].to<JsonObject>();
                bn["rssi"]   = _wifi_rssi;
                bn["uptime"] = _uptime_sec;
            }
            break;
        }
        case EVT_STARTUP:
        case EVT_STATUS_UPDATE: {
            JsonObject e0   = events.add<JsonObject>();
            e0["device"]    = _cs_net_cfg.agent_uuid;
            e0["time"]      = ts;
            e0["event"]     = (evt == EVT_STARTUP) ? "core/startup" : "core/status-updated";
            JsonObject body = e0["body"].to<JsonObject>();
            body["hw_type"]       = "ferp-com";
            body["hw_version"]    = "2602";
            body["sw_version"]    = "1.0.0";
            body["local_ip"]      = _wifi_ip;
            body["mac"]           = _wifi_mac;
            body["wifi_ssid"]     = _wifi_ssid;
            body["wifi_password"] = _wifi_password;
            body["sd_status"]     = "unknown";
            break;
        }
        case EVT_RECONNECT: {
            JsonObject e0   = events.add<JsonObject>();
            e0["device"]    = _cs_net_cfg.agent_uuid;
            e0["time"]      = ts;
            e0["event"]     = "core/reconnect";
            JsonObject body = e0["body"].to<JsonObject>();
            body["rssi"]          = _wifi_rssi;
            body["uptime"]        = _uptime_sec;
            body["local_ip"]      = _wifi_ip;
            body["wifi_ssid"]     = _wifi_ssid;
            body["wifi_password"] = _wifi_password;
            break;
        }
        case EVT_PUMP_START: {
            // app.fuel/pump-start — sent when a nozzle transitions to PUMPING
            if (_pump_start_nozzle_idx >= CS_NO_NOZZLES ||
                _cs_nozzles[_pump_start_nozzle_idx].uuid[0] == '\0') return false;
            JsonObject e0   = events.add<JsonObject>();
            e0["device"]    = _cs_nozzles[_pump_start_nozzle_idx].uuid;
            e0["time"]      = ts;
            e0["event"]     = "app.fuel/pump-start";
            JsonObject body = e0["body"].to<JsonObject>();
            body["Temp"]    = "Empty";
            break;
        }
        case EVT_PRINT_OK: {
            if (_print_ok_nozzle_idx >= CS_NO_NOZZLES ||
                _cs_nozzles[_print_ok_nozzle_idx].uuid[0] == '\0') return false;
            // Use current wall-clock time (when the print was confirmed), same as old firmware.
            struct timeval now_tv;
            gettimeofday(&now_tv, nullptr);
            char pok_ts[64] = {};
            _cs_format_iso8601(now_tv.tv_sec + (int)(3600 * 5.5), "+05:30", pok_ts, sizeof(pok_ts));
            JsonObject e0   = events.add<JsonObject>();
            e0["device"]    = _cs_nozzles[_print_ok_nozzle_idx].uuid;
            e0["time"]      = pok_ts;
            e0["event"]     = "app.fuel/printok";
            JsonObject body = e0["body"].to<JsonObject>();
            if (_print_ok_ne_id != 0) {
                char ne_id_str[24] = {};
                snprintf(ne_id_str, sizeof(ne_id_str), "%llu",
                         (unsigned long long)_print_ok_ne_id);
                body["NE_ID"]  = ne_id_str;
                body["ABS_ID"] = _print_ok_dispenser_event_id;
            }
            break;
        }
        case EVT_PUMPED:
            // JSON is built by _build_pumped_event_json() which serialises directly into _event_json
            return _build_pumped_event_json(
                _last_pumped_nozzle_idx,
                _last_pumped_vol_lx1000,
                _last_pumped_unit_px100,
                _last_pumped_total_px100,
                _last_pumped_ts_epoch,
                _last_pumped_event_id,
                _last_pumped_ne_id);
        default: return false;
    }

    size_t written = serializeJson(doc, _event_json, sizeof(_event_json));
    LOG_MSG_DEBUG(CSP_LOG_EN, "built JSON for event %d: %s", (int)evt, _event_json);
    return (written > 0);
}

bool ModuleCubeSphere::_build_pumped_event_json(
    uint8_t  nozzle_idx,
    uint32_t vol_lx1000,
    uint32_t unit_pricex100,
    uint64_t total_pricex100,
    time_t   ts_epoch,
    uint32_t event_id,
    uint64_t ne_id)
{
    if (nozzle_idx >= CS_NO_NOZZLES || _cs_nozzles[nozzle_idx].uuid[0] == '\0') return false;

    char ts[64] = {};
    _cs_format_iso8601(ts_epoch + (int)(3600 * 5.5), "+05:30", ts, sizeof(ts));

    JsonDocument doc;
    JsonObject root   = doc.to<JsonObject>();
    JsonArray  events = root["events"].to<JsonArray>();
    JsonObject e0     = events.add<JsonObject>();
    e0["device"] = _cs_nozzles[nozzle_idx].uuid;
    e0["time"]   = ts;
    e0["event"]  = "app.fuel/pump-end";
    JsonObject body = e0["body"].to<JsonObject>();
    body["L"] = vol_lx1000      * 0.001;
    body["T"] = _cs_nozzles[nozzle_idx].fuel_type;
    body["P"] = total_pricex100 * 0.01;
    body["U"] = unit_pricex100  * 0.01;
    if (ne_id != 0) {
        char ne_id_str[24] = {};
        snprintf(ne_id_str, sizeof(ne_id_str), "%llu", (unsigned long long)ne_id);
        body["ID"]     = ne_id_str;
        body["ABS_ID"] = event_id;
    }

    size_t written = serializeJson(doc, _event_json, sizeof(_event_json));
    LOG_MSG_DEBUG(CSP_LOG_EN, "built pumped JSON (nozzle=%u neid=%llu): %s",
                  nozzle_idx, (unsigned long long)ne_id, _event_json);
    return (written > 0);
}

void ModuleCubeSphere::_on_event_result(const hsys_msg_t &msg)
{
    auto f = MsgHttpResult::get_fields(msg);
    bool ok = (f.result == HTTP_RESULT_SUCCESS &&
               (f.status_code == 200 || f.status_code == 201));

    if (ok && f.body && f.body_len > 0) {
        JsonDocument doc;
        deserializeJson(doc, (const char *)f.body, DeserializationOption::NestingLimit(10));
        if (doc["data"].is<JsonArray>()) {
            JsonArray arr = doc["data"].as<JsonArray>();
            if (arr.size() > 0) {
                const char *st = arr[0]["status"] | "";
                ok = (strcmp(st, "OK") == 0);
            }
        }
    }

    switch (_cur_evt) 
    {
        case EVT_HEARTBEAT:
            if (ok) {
                _publish_status(CUBESPHERE_STATUS_HB_SENT);
                LOG_MSG_INFO(CSP_LOG_EN, "heartbeat OK");
            } else {
                LOG_MSG_WARNING(CSP_LOG_EN, "heartbeat failed result=%d status=%d",
                                (int)f.result, (int)f.status_code);
                LOG_MSG_DEBUG(CSP_LOG_EN, "heartbeat response body: %.*s",
                              (int)f.body_len, f.body ? (const char *)f.body : "(null)");
                _publish_status(CUBESPHERE_STATUS_HB_FAILED);
            }
            break;
        case EVT_STARTUP:
            if (ok) {
                LOG_MSG_INFO(CSP_LOG_EN, "startup event OK");
            } else {
                LOG_MSG_WARNING(CSP_LOG_EN, "startup event failed result=%d status=%d — will retry in 10s",
                                (int)f.result, (int)f.status_code);
                LOG_MSG_DEBUG(CSP_LOG_EN, "startup response body: %.*s",
                              (int)f.body_len, f.body ? (const char *)f.body : "(null)");
                _pending_startup = true;
                _cur_evt = EVT_NONE;
                _arm_timer(10000);
                return;
            }
            break;
        case EVT_RECONNECT:
            if (ok) {
                LOG_MSG_INFO(CSP_LOG_EN, "reconnect event OK");
            } else {
                LOG_MSG_WARNING(CSP_LOG_EN, "reconnect event failed result=%d status=%d — will retry in 10s",
                                (int)f.result, (int)f.status_code);
                _pending_reconnect = true;
                _cur_evt = EVT_NONE;
                _arm_timer(10000);
                return;
            }
            break;
        case EVT_PUMPED:
            if (ok) {
                _pumped_success++;
                _publish_status(CUBESPHERE_STATUS_PUMPED_SUCCESS);
                LOG_MSG_INFO(CSP_LOG_EN, "pumped OK (total=%lu)", (unsigned long)_pumped_success);
            } else {
                _pumped_failure++;
                LOG_MSG_WARNING(CSP_LOG_EN, "pumped failed result=%d status=%d",
                                (int)f.result, (int)f.status_code);
                LOG_MSG_DEBUG(CSP_LOG_EN, "pumped response body: %.*s",
                              (int)f.body_len, f.body ? (const char *)f.body : "(null)");
                _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED);
            }
            break;
        case EVT_PUMPED_RETX:
            _retx_in_progress = false;
            if (ok) {
                // Acknowledge only after the HTTP response confirms success.
                // The item stays in the list if we fail, so it will be retried.
                list_mgr_ack(&_retx_mgr.list_mgr, &_retx_pending_rh);
                _pumped_success++;
                _publish_status(CUBESPHERE_STATUS_PUMPED_SUCCESS);
                LOG_MSG_INFO(CSP_LOG_EN, "retx pumped OK — trying next");
                // Clear _cur_evt BEFORE _retx_try_send_one() so it can set
                // EVT_PUMPED_RETX again if another item is queued.  If it does
                // start a send we return early to avoid overwriting _cur_evt.
                _cur_evt = EVT_NONE;
                if (_system_is_idle && _internet_connected && !_retx_last_send_failed
                    && _retx_try_send_one()) {
                    return;  // next retx in flight — _cur_evt already set
                }
                _start_next_event();
                return;
            }
            // Send failed — leave item in list; _on_tick() clears the flag after 60 s
            _retx_last_send_failed = true;
            _pumped_failure++;
            LOG_MSG_WARNING(CSP_LOG_EN, "retx pumped failed — will retry in ~60s");
            _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED);
            break;
        case EVT_PUMP_START:
            if (ok) {
                LOG_MSG_INFO(CSP_LOG_EN, "pump-start OK (nozzle=%u)",
                             (unsigned)_pump_start_nozzle_idx);
            } else {
                LOG_MSG_WARNING(CSP_LOG_EN, "pump-start failed result=%d status=%d",
                                (int)f.result, (int)f.status_code);
            }
            break;
        case EVT_PRINT_OK:
            if (ok) {
                LOG_MSG_INFO(CSP_LOG_EN, "printok OK (nozzle=%u abs_id=%lu)",
                             (unsigned)_print_ok_nozzle_idx,
                             (unsigned long)_print_ok_dispenser_event_id);
            } else {
                LOG_MSG_WARNING(CSP_LOG_EN, "printok failed result=%d status=%d",
                                (int)f.result, (int)f.status_code);
            }
            break;
        default: break;
    }

    _cur_evt = EVT_NONE;
    LOG_MSG_DEBUG(CSP_LOG_EN, "event result processing complete, checking for next event"); 
    _start_next_event();
}

// ── HTTP session helpers ──────────────────────────────────────────────────────

void ModuleCubeSphere::_begin_http_request(pal_http_method_t method,
                                            uint32_t          timeout_ms,
                                            const char       *url,
                                            const char       *auth_value,
                                            const char       *body,
                                            uint32_t          body_len,
                                            const char       *collect_key)
{
    // Build packed headers: Authorization + Content-Type (POST only)
    static char s_hdrs_buf[512];
    uint16_t    hdrs_len = 0U;

    if (auth_value && auth_value[0] != '\0') {
        // "Authorization\0<value>\0"
        size_t klen = strlen("Authorization");
        size_t vlen = strlen(auth_value);
        if (hdrs_len + klen + 1U + vlen + 1U <= sizeof(s_hdrs_buf)) {
            memcpy(s_hdrs_buf + hdrs_len, "Authorization", klen + 1U);
            hdrs_len += (uint16_t)(klen + 1U);
            memcpy(s_hdrs_buf + hdrs_len, auth_value, vlen + 1U);
            hdrs_len += (uint16_t)(vlen + 1U);
        }
    }
    if (body && body_len > 0U) {
        // "Content-Type\0application/json\0"
        const char *ct_k = "Content-Type";
        const char *ct_v = "application/json";
        size_t klen = strlen(ct_k);
        size_t vlen = strlen(ct_v);
        if (hdrs_len + klen + 1U + vlen + 1U <= sizeof(s_hdrs_buf)) {
            memcpy(s_hdrs_buf + hdrs_len, ct_k, klen + 1U);
            hdrs_len += (uint16_t)(klen + 1U);
            memcpy(s_hdrs_buf + hdrs_len, ct_v, vlen + 1U);
            hdrs_len += (uint16_t)(vlen + 1U);
        }
    }

    if (body_len > MSG_HTTP_REQUEST_MAX_BODY_LEN) {
        LOG_MSG_WARNING(CSP_LOG_EN, "_begin_http_request: body truncated %lu→%u",
                        (unsigned long)body_len, (unsigned)MSG_HTTP_REQUEST_MAX_BODY_LEN);
        body_len = MSG_HTTP_REQUEST_MAX_BODY_LEN;
    }

    hsys_msg_t *m = MsgHttpRequest::create(
        id(), method, timeout_ms,
        global_root_ca, url,
        hdrs_len > 0U ? s_hdrs_buf : nullptr, hdrs_len,
        body, (uint16_t)body_len,
        collect_key);

    if (!m) {
        LOG_MSG_ERROR(CSP_LOG_EN, "_begin_http_request: create failed — retry in 2s");
        _http_phase = HTTP_IDLE;
        _arm_timer(2000);
        return;
    }

    send(m, MODULE_HTTP_ID);
    _http_phase      = HTTP_EXECUTING;
    _http_exec_ticks = 0;
}


void ModuleCubeSphere::set_storage(const storage_interface_t *storage) 
{ 
    _storage = storage; 
    LOG_MSG_DEBUG(CSP_LOG_EN, "storage interface set: _storage=%lld", _storage);
}

/**
 * Set the application-wide root CA certificate (from app_rootca.h).
 * Used as fallback when no per-session root CA is provided via cloud config.
 * Must be called before app_init() triggers init().
 * The pointer must remain valid for the lifetime of the module.
 */
void ModuleCubeSphere::set_root_ca(char *ca) { 
    
    // LOG_MSG_INFO(CSP_LOG_EN, "root CA set via set_root_ca() — will be used for all HTTP sessions without per-session CA %lld", 
    //              (unsigned long long)ca);
    // _static_root_ca = ca; 

    // LOG_MSG_DEBUG(CSP_LOG_EN, "static root CA:\n%s", _static_root_ca ? _static_root_ca : "(null)");
}

void ModuleCubeSphere::_arm_timer(uint32_t duration_ms)
{
    MsgTimerStart::Payload p{};
    p.source_module_id = id();
    p.start_offset_ms  = 0;
    p.duration_ms      = duration_ms;
    p.is_repetitive    = false;
    p.forced           = true;
    auto *msg = create_typed<MsgTimerStart>(p);
    LOG_MSG_DEBUG(CSP_LOG_EN, "arming timer for %u ms", (unsigned)duration_ms);
    if (msg) publish(msg);
}

// ── Payload builders ──────────────────────────────────────────────────────────

cs_startup_info_t ModuleCubeSphere::_build_startup_info() const
{
    cs_startup_info_t s = {};
    strncpy(s.ssid,           _wifi_ssid,    sizeof(s.ssid)           - 1);
    strncpy(s.password,       _wifi_password,sizeof(s.password)       - 1);
    strncpy(s.ip_address,     _wifi_ip,      sizeof(s.ip_address)     - 1);
    strncpy(s.mac_address_str,_wifi_mac,     sizeof(s.mac_address_str)- 1);
    strncpy(s.fw_version,     "1.0.0",       sizeof(s.fw_version)     - 1);
    strncpy(s.hw_version,     "2602",        sizeof(s.hw_version)     - 1);
    strncpy(s.board_version,  "2602",        sizeof(s.board_version)  - 1);
    strncpy(s.device_type,    "ferp-com",    sizeof(s.device_type)    - 1);
    strncpy(s.sd_card_status, "unknown",     sizeof(s.sd_card_status) - 1);
    s.rssi               = _wifi_rssi;
    s.uptime_sec         = _uptime_sec;
    s.event_count_success = _pumped_success;
    s.event_count_failure = _pumped_failure;
    return s;
}

cs_hb_info_t ModuleCubeSphere::_build_hb_info() const
{
    cs_hb_info_t hb = {};
    hb.rssi               = _wifi_rssi;
    hb.uptime_sec         = _uptime_sec;
    hb.event_count_success = _pumped_success;
    hb.event_count_failure = _pumped_failure;
    return hb;
}

// ── Status publish ────────────────────────────────────────────────────────────

void ModuleCubeSphere::_publish_status(cubesphere_status_event_t ev,
                                        uint8_t nozzle_idx,
                                        const char *uuid)
{
    MsgCubesphereStatus::Payload p{};
    p.event      = ev;
    p.nozzle_idx = nozzle_idx;
    if (uuid) strncpy(p.device_uuid, uuid, sizeof(p.device_uuid) - 1);
    auto *msg = create_typed<MsgCubesphereStatus>(p);
    LOG_MSG_DEBUG(CSP_LOG_EN, "publishing status event %d (nozzle_idx=%u uuid=%s)",
                  (int)ev, (unsigned)nozzle_idx, uuid ? uuid : "(null)");
    if (msg) publish(msg);
}

// ── CubeSphere crypto helpers ─────────────────────────────────────────────────

void ModuleCubeSphere::_cs_get_sha256_hex(const uint8_t *data, size_t len,
                                           char *out, size_t out_len)
{
    uint8_t hash[PAL_SHA256_DIGEST_LENGTH] = {};
    pal_crypto_sha256(data, len, hash);
    pal_crypto_bin_to_hex(hash, PAL_SHA256_DIGEST_LENGTH, out, out_len);
}

void ModuleCubeSphere::_cs_calc_sha256(const char *nonce, const char *mac,
                                        const char *key, char *out, size_t out_len)
{
    char sha_mac[PAL_SHA256_DIGEST_LENGTH * 2 + 1] = {};
    _cs_get_sha256_hex((const uint8_t *)mac, strlen(mac), sha_mac, sizeof(sha_mac));

    size_t total = strlen(sha_mac) + strlen(key) + strlen(nonce);
    char  *buf   = (char *)malloc(total + 1);
    if (!buf) { out[0] = '\0'; return; }
    snprintf(buf, total + 1, "%s%s%s", sha_mac, key, nonce);
    _cs_get_sha256_hex((const uint8_t *)buf, strlen(buf), out, out_len);
    free(buf);
}

void ModuleCubeSphere::_cs_format_iso8601(time_t epoch_sec, const char *tz_offset,
                                           char *buf, size_t buf_len)
{
    struct tm tm_info;
    gmtime_r(&epoch_sec, &tm_info);
    snprintf(buf, buf_len, "%04d-%02d-%02dT%02d:%02d:%02d.000%s",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, tz_offset);
}

// ── Retransmission ────────────────────────────────────────────────────────────

void ModuleCubeSphere::_on_sd_ready()
{
    LOG_MSG_INFO(CSP_LOG_EN, "SD ready — initialising retx manager");
    _retx_init();
}

void ModuleCubeSphere::_retx_init()
{
    if (_retx_ready || !_storage) return;

    retx_manager_config_t cfg = {};
    cfg.list_config.storage            = const_cast<storage_interface_t *>(_storage);
    cfg.list_config.parent_path        = "/retx";
    cfg.list_config.file_prefix        = "evt";
    cfg.list_config.max_lines_per_file = 500;
    cfg.list_config.max_tracked_days   = 7;
    cfg.send_callback = [](retx_event_type_t type, const char *payload,
                           size_t len, void *ud) -> int32_t {
        auto *self = static_cast<ModuleCubeSphere *>(ud);
        if (self->_state != STATE_RUNNING) return -1;
        // TODO: deserialise JSON and call _cs_send_pumped()
        (void)type; (void)payload; (void)len;
        return -1;
    };
    cfg.user_data         = this;
    cfg.retry_interval_ms = 60000;

    if (retx_mgr_init(&_retx_mgr, &cfg) == RETX_MGR_OK) 
    {
        _retx_ready = true;
        LOG_MSG_INFO(CSP_LOG_EN, "retx: ready");
    } 
    else 
    {
        LOG_MSG_ERROR(CSP_LOG_EN, "retx: init failed");
    }
}

void ModuleCubeSphere::_retx_store_pumped(const MsgFuelPumped::Payload &p)
{
    if (!_retx_ready) {
        LOG_MSG_WARNING(CSP_LOG_EN, "retx: not ready — event lost");
        return;
    }

    char json[256];
    snprintf(json, sizeof(json),
             "{\"nozzle\":%u,\"vol\":%lu,\"unit\":%lu,\"total\":%lu,\"ts\":%llu,\"neid\":\"%llu\"}",
             (unsigned)p.nozzle_idx, (unsigned long)p.vol_lx1000,
             (unsigned long)p.unit_pricex100, (unsigned long)p.total_pricex100,
             (unsigned long long)p.time_stamp, (unsigned long long)p.ne_id);

    char date_key[16] = "00000000";
    time_t now = time(nullptr);
    struct tm tm_info = {};
    localtime_r(&now, &tm_info);
    strftime(date_key, sizeof(date_key), "%Y%m%d", &tm_info);

    retx_mgr_add_failed_event(&_retx_mgr, date_key, RETX_EVENT_TYPE_PUMPED,
                               json, strlen(json));
}

void ModuleCubeSphere::_retx_process_one()
{
    if (!_retx_ready || _state != STATE_RUNNING) return;
    retx_mgr_process(&_retx_mgr);
}

bool ModuleCubeSphere::_retx_try_send_one()
{
    // Guard: only proceed when all conditions are met
    if (!_retx_ready || !_retx_mgr.is_initialized) return false;
    if (_state != STATE_RUNNING)       return false;
    if (!_system_is_idle)              return false;
    if (!_internet_connected)          return false;
    if (_retx_in_progress)             return false;
    // _retx_last_send_failed is cleared by _on_tick() every 60 s; guard here
    // prevents the system-idle triggered path from bypassing the wait.
    if (_retx_last_send_failed)        return false;
    if (_http_phase != HTTP_IDLE)      return false;

    // Peek at the next unprocessed retransmit event
    char buf[RETX_MGR_MAX_EVENT_SIZE + 64];
    list_mgr_read_handle_t rh = {};
    int32_t ret = list_mgr_peek_next(&_retx_mgr.list_mgr, buf, sizeof(buf), &rh);
    if (ret != 0) {
        // No data or read error — nothing to do
        return false;
    }

    // Deserialise the stored payload (compact JSON written by _retx_store_pumped)
    // Format: {"nozzle":<n>,"vol":<v>,"unit":<u>,"total":<t>,"ts":<epoch>}
    // We need to rebuild it as a CubeSphere pump-end event.
    int      type_int;
    unsigned payload_len;
    const char *pipe2 = buf;
    {
        int pipes = 0;
        while (*pipe2 && pipes < 2) { if (*pipe2++ == '|') pipes++; }
    }
    if (sscanf(buf, "%d|%u|", &type_int, &payload_len) != 2 || payload_len == 0) {
        LOG_MSG_ERROR(CSP_LOG_EN, "retx: corrupted record — skipping");
        list_mgr_ack(&_retx_mgr.list_mgr, &rh);   // discard bad record
        return false;
    }

    // pipe2 now points to the JSON payload
    JsonDocument pdoc;
    if (deserializeJson(pdoc, pipe2, payload_len) != DeserializationError::Ok) {
        LOG_MSG_ERROR(CSP_LOG_EN, "retx: JSON parse error — skipping");
        list_mgr_ack(&_retx_mgr.list_mgr, &rh);
        return false;
    }

    uint8_t  nozzle_idx       = (uint8_t)pdoc["nozzle"].as<int>();
    uint32_t vol_lx1000       = (uint32_t)pdoc["vol"].as<long>();
    uint32_t unit_pricex100   = (uint32_t)pdoc["unit"].as<long>();
    uint64_t total_pricex100  = (uint64_t)pdoc["total"].as<long long>();
    time_t   ts_epoch         = (time_t)pdoc["ts"].as<long long>();
    uint64_t ne_id            = 0;
    if (pdoc["neid"].is<const char *>()) {
        const char *neid_str = pdoc["neid"].as<const char *>();
        if (neid_str) ne_id = (uint64_t)strtoull(neid_str, nullptr, 10);
    }

    if (nozzle_idx >= CS_NO_NOZZLES) {
        LOG_MSG_ERROR(CSP_LOG_EN, "retx: nozzle_idx %u out of range — discarding", nozzle_idx);
        list_mgr_ack(&_retx_mgr.list_mgr, &rh);
        return false;
    }
    if (_cs_nozzles[nozzle_idx].uuid[0] == '\0') {
        LOG_MSG_WARNING(CSP_LOG_EN, "retx: nozzle_idx %u not provisioned — discarding", nozzle_idx);
        list_mgr_ack(&_retx_mgr.list_mgr, &rh);
        return false;
    }

    if (!_build_pumped_event_json(nozzle_idx, vol_lx1000, unit_pricex100,
                                   total_pricex100, ts_epoch, 0, ne_id)) {
        LOG_MSG_ERROR(CSP_LOG_EN, "retx: failed to build event JSON — skipping");
        list_mgr_ack(&_retx_mgr.list_mgr, &rh);
        return false;
    }

    // Store read handle so we can ack on success (ONLY after HTTP result confirms OK)
    _retx_pending_rh  = rh;
    _retx_in_progress = true;
    _cur_evt          = EVT_PUMPED_RETX;

    {
        char auth[512] = {};
        snprintf(auth, sizeof(auth), "Basic %s", _cs_net_cfg.basic_authentication_base64);
        _begin_http_request(PAL_HTTP_METHOD_POST, 10000,
                            CS_URL_EVENTS, auth,
                            _event_json, (uint32_t)strlen(_event_json), nullptr);
    }
    LOG_MSG_INFO(CSP_LOG_EN, "retx: sending pumped event for nozzle %u", nozzle_idx);
    return true;
}
