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
#include "msg_timer_start.h"
#include "msg_timer_alarm.h"
#include "msg_sd_ready.h"
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

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleCubeSphere::init()
{
    subscribe(MsgConfigReady::ID);
    subscribe(MsgConfigCloud::ID);
    subscribe(MsgConfigWifi::ID);
    subscribe(MsgWifiEvent::ID);
    subscribe(MsgInternetStatus::ID);
    subscribe(MsgFuelPumped::ID);
    subscribe(MsgTimerAlarm::ID);
    subscribe(MsgSdReady::ID);
    subscribe(MSG_ID_TICK_1000MS);

    LOG_MSG_INFO(CSP_LOG_EN, "init — state=WAIT_FOR_INTERNET");
}

// ── Message dispatcher ────────────────────────────────────────────────────────

void ModuleCubeSphere::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MsgConfigReady::ID:     _on_config_ready();          break;
        case MsgConfigCloud::ID:     _on_config_cloud(msg);       break;
        case MsgConfigWifi::ID:      _on_config_wifi(msg);        break;
        case MsgWifiEvent::ID:       _on_wifi_event(msg);         break;
        case MsgInternetStatus::ID:  _on_internet_status(msg);    break;
        case MsgFuelPumped::ID:      _on_fuel_pumped(msg);        break;
        case MsgSdReady::ID:         _on_sd_ready();              break;
        case MsgTimerAlarm::ID:      _on_timer_alarm();           break;
        case MSG_ID_TICK_1000MS:     _on_tick();                  break;
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
    _cloud_root_ca = p.root_ca;
    if (p.hb_interval_s > 0) _hb_interval_ms = p.hb_interval_s * 1000UL;
    _hb_enabled = p.hb_enabled;
    _cloud_config_ready = true;

    LOG_MSG_INFO(CSP_LOG_EN, "cloud config: root_ca=%s hb_enabled=%d interval=%us",
                 _cloud_root_ca ? "***" : "(null)", (int)p.hb_enabled, (unsigned)p.hb_interval_s);

    if (_internet_up && _state == STATE_WAIT_FOR_INTERNET) {
        _state = STATE_REGISTERING;
        _attempt_registration();
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
    _internet_up = p.connected;
    LOG_MSG_INFO(CSP_LOG_EN, "internet %s", p.connected ? "UP" : "DOWN");

    if (p.connected) {
        if (_state == STATE_WAIT_FOR_INTERNET) {
            if (_cloud_config_ready) {
                _state = STATE_REGISTERING;
                _attempt_registration();
            } else {
                LOG_MSG_INFO(CSP_LOG_EN, "internet up — waiting for cloud config");
            }
        }
    } else {
        if (_state == STATE_RUNNING) {
            LOG_MSG_WARNING(CSP_LOG_EN, "internet lost — back to WAIT_FOR_INTERNET");
            _state = STATE_WAIT_FOR_INTERNET;
        }
    }
}

void ModuleCubeSphere::_on_fuel_pumped(const hsys_msg_t &msg)
{
    auto p = MsgFuelPumped::deserialize(msg);

    if (_state != STATE_RUNNING) {
        LOG_MSG_WARNING(CSP_LOG_EN, "fuel pumped but not RUNNING — storing for retx");
        _pumped_failure++;
        _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED, p.nozzle_idx);
        _retx_store_pumped(p, nullptr);
        return;
    }

    cs_pumped_event_t ev = {};
    ev.nozzle_idx     = p.nozzle_idx;
    ev.time_stamp     = (uint64_t)time(nullptr);
    ev.unit_pricex100  = p.unit_pricex100;
    ev.total_pricex100 = p.total_pricex100;
    ev.volume_lx1000   = p.vol_lx1000;
    ev.event_id        = _pumped_success + _pumped_failure + 1;

    int32_t ret = _cs_send_pumped(ev);
    if (ret == CS_ERROR_OK) {
        _pumped_success++;
        _publish_status(CUBESPHERE_STATUS_PUMPED_SUCCESS, p.nozzle_idx);
        LOG_MSG_INFO(CSP_LOG_EN, "pumped sent OK (total=%lu)", (unsigned long)_pumped_success);
    } else {
        _pumped_failure++;
        LOG_MSG_ERROR(CSP_LOG_EN, "pumped FAILED ret=%d — storing for retx", ret);
        _publish_status(CUBESPHERE_STATUS_PUMPED_FAILED, p.nozzle_idx);
        _retx_store_pumped(p, nullptr);
    }
}

void ModuleCubeSphere::_on_timer_alarm()
{
    switch (_state) {
        case STATE_REGISTERING:
            LOG_MSG_INFO(CSP_LOG_EN, "retry timer — re-attempting registration");
            _attempt_registration();
            break;
        case STATE_RUNNING:
            _pending_heartbeat = true;
            _process_events();
            _retx_process_one();
            break;
        default: break;
    }
}

void ModuleCubeSphere::_on_tick() { _uptime_sec++; }

// ── State machine ─────────────────────────────────────────────────────────────

void ModuleCubeSphere::_attempt_registration()
{
    uint8_t mac_bytes[6] = {};
    char    mac12[13]    = {};
    if (pal_efuse_get_mac(mac_bytes, sizeof(mac_bytes)) == PAL_OK) {
        snprintf(mac12, sizeof(mac12), "%02X%02X%02X%02X%02X%02X",
                 mac_bytes[0], mac_bytes[1], mac_bytes[2],
                 mac_bytes[3], mac_bytes[4], mac_bytes[5]);
    } else {
        LOG_MSG_ERROR(CSP_LOG_EN, "pal_efuse_get_mac failed");
    }

    LOG_MSG_INFO(CSP_LOG_EN, "attempting registration mac=%s", mac12);

    int32_t ret = _cs_register(mac12, _cloud_root_ca);
    if (ret == CS_ERROR_OK) {
        LOG_MSG_INFO(CSP_LOG_EN, "registration OK — state=RUNNING");
        _state = STATE_RUNNING;
        _pending_startup = true;
        _process_events();

        _publish_status(CUBESPHERE_STATUS_REGISTERED, 0, _cs_net_cfg.agent_uuid);
        if (_hb_enabled) _arm_timer(_hb_interval_ms);
    } else {
        LOG_MSG_ERROR(CSP_LOG_EN, "registration FAILED ret=%d — retry in %lus",
                      ret, (unsigned long)(MODULE_CUBESPHERE_RETRY_INTERVAL_MS / 1000));
        _publish_status(CUBESPHERE_STATUS_REGISTER_FAILED);
        _arm_timer(MODULE_CUBESPHERE_RETRY_INTERVAL_MS);
    }
}

void ModuleCubeSphere::_process_events()
{
    if (_state != STATE_RUNNING) return;

    if (_pending_startup) {
        _pending_startup = false;
        LOG_MSG_INFO(CSP_LOG_EN, "sending startup event");
        _cs_send_startup(_build_startup_info());
    }

    if (_pending_reconnect) {
        _pending_reconnect = false;
        LOG_MSG_INFO(CSP_LOG_EN, "sending reconnect event");
        cs_reconnect_info_t r = {};
        strncpy(r.ssid,       _wifi_ssid,     sizeof(r.ssid)       - 1);
        strncpy(r.password,   _wifi_password, sizeof(r.password)   - 1);
        strncpy(r.ip_address, _wifi_ip,       sizeof(r.ip_address) - 1);
        r.rssi       = _wifi_rssi;
        r.uptime_sec = _uptime_sec;
        _cs_send_reconnect(r);
    }

    if (_pending_status_update) {
        _pending_status_update = false;
        LOG_MSG_INFO(CSP_LOG_EN, "sending status-updated event");
        _cs_send_status_updated(_build_startup_info());
    }

    if (_pending_heartbeat && _hb_enabled) {
        _pending_heartbeat = false;
        LOG_MSG_INFO(CSP_LOG_EN, "sending heartbeat");
        int32_t ret = _cs_send_hb(_build_hb_info());
        if (ret == CS_ERROR_OK) {
            _publish_status(CUBESPHERE_STATUS_HB_SENT);
        } else {
            LOG_MSG_WARNING(CSP_LOG_EN, "heartbeat failed ret=%d", ret);
            _publish_status(CUBESPHERE_STATUS_HB_FAILED);
        }
        _arm_timer(_hb_interval_ms);
    }
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

// ── CubeSphere HTTP session methods ───────────────────────────────────────────

int32_t ModuleCubeSphere::_cs_register(const char *mac12, const char *root_ca)
{
    int32_t ret = CS_ERROR_INVALID_MAC;

    do {
        if (strlen(mac12) != 12) {
            LOG_MSG_DEBUG(CSP_LOG_EN, "mac12 invalid length");
            break;
        }

        // ── Request 1: GET bootstrap (401 + nonce) ────────────────────────────
        pal_http_client_config_t cfg = {};
        cfg.url        = "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/bootstrap/core/v1/device";;
        cfg.cert_pem   = root_ca;
        cfg.timeout_ms = 30000;
        cfg.keep_alive = false;

        pal_http_client_handle_t h = nullptr;
        if (pal_http_client_init(&cfg, &h) != 0) {
            LOG_MSG_ERROR(CSP_LOG_EN, "req1 init failed");
            ret = CS_ERROR_NO_NONCE;
            break;
        }

        const char *hdr_names[] = { "www-authenticate", "WWW-Authenticate" };
        pal_http_client_collect_headers(h, hdr_names, 2);

        pal_http_response_t resp = {};
        int32_t sc = pal_http_client_get(h, &resp);

        bool  nonce_ok = false;
        char  nonce[128] = {};
        if (sc == 401) {
            char hdr[512] = {};
            if (pal_http_client_get_header(h, "www-authenticate", hdr, sizeof(hdr)) != 0)
                pal_http_client_get_header(h, "WWW-Authenticate", hdr, sizeof(hdr));

            char *ns = strstr(hdr, "nonce=\"");
            if (ns) {
                ns += 7;
                char *ne = strchr(ns, '"');
                if (ne && (size_t)(ne - ns) < sizeof(nonce)) {
                    memcpy(nonce, ns, ne - ns);
                    nonce_ok = true;
                }
            }
        }

        pal_http_response_free(&resp);
        pal_http_client_cleanup(h);
        h = nullptr;

        if (!nonce_ok) { ret = CS_ERROR_NO_NONCE; break; }
        LOG_MSG_DEBUG(CSP_LOG_EN, "nonce obtained");

        // ── Request 2: GET bootstrap with SAS-AC1 auth ────────────────────────
        char token[PAL_SHA256_DIGEST_LENGTH * 2 + 1] = {};
        _cs_calc_sha256(nonce, mac12, _cs_key, token, sizeof(token));

        cfg.url = "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/bootstrap/core/v1/device";;
        if (pal_http_client_init(&cfg, &h) != 0) {
            LOG_MSG_ERROR(CSP_LOG_EN, "req2 init failed");
            ret = CS_ERROR_GET_AGENT_CONFIG_FAILED;
            break;
        }

        char auth[512] = {};
        snprintf(auth, sizeof(auth), "SAS-AC1 nonce=\"%s\" id=\"%s\" token=\"%s\"",
                 nonce, mac12, token);
        pal_http_client_set_header(h, "Authorization", auth);

        sc = pal_http_client_get(h, &resp);
        LOG_MSG_DEBUG(CSP_LOG_EN, "req2 status=%d", sc);

        if (sc == 200 || sc == 201) {
            JsonDocument doc;
            deserializeJson(doc, resp.body, DeserializationOption::NestingLimit(20));
            if (doc.containsKey("data")) {
                JsonObject data = doc["data"].as<JsonObject>();
                const char *dev_id = data["device_id"] | (const char *)nullptr;
                const char *secret = data["secret"]    | (const char *)nullptr;

                if (!dev_id) {
                    LOG_MSG_DEBUG(CSP_LOG_EN, "no device_id in response");
                    pal_http_response_free(&resp);
                    pal_http_client_cleanup(h);
                    ret = CS_ERROR_GET_AGENT_CONFIG_FAILED;
                    break;
                }
                if (!secret) {
                    LOG_MSG_DEBUG(CSP_LOG_EN, "no secret in response");
                    pal_http_response_free(&resp);
                    pal_http_client_cleanup(h);
                    ret = CS_ERROR_GET_AGENT_CONFIG_FAILED;
                    break;
                }

                // Build basic auth: base64(device_id:secret)
                char id_secret[CS_SIZE_UUID + CS_SIZE_SECRET + 2] = {};
                snprintf(id_secret, sizeof(id_secret), "%s:%s", dev_id, secret);
                char b64[CS_SIZE_SECRET] = {};
                pal_crypto_base64_encode((const uint8_t *)id_secret, strlen(id_secret),
                                         b64, sizeof(b64));

                memset(&_cs_net_cfg, 0, sizeof(_cs_net_cfg));
                strncpy(_cs_net_cfg.agent_uuid,                  dev_id, CS_SIZE_UUID   - 1);
                strncpy(_cs_net_cfg.basic_authentication_base64, b64,    CS_SIZE_SECRET - 1);
                LOG_MSG_DEBUG(CSP_LOG_EN, "device_id=%s", _cs_net_cfg.agent_uuid);
            } else {
                pal_http_response_free(&resp);
                pal_http_client_cleanup(h);
                ret = CS_ERROR_GET_AGENT_CONFIG_FAILED;
                break;
            }
        } else {
            pal_http_response_free(&resp);
            pal_http_client_cleanup(h);
            ret = CS_ERROR_GET_AGENT_CONFIG_FAILED;
            break;
        }

        pal_http_response_free(&resp);
        pal_http_client_cleanup(h);
        h = nullptr;

        // ── Request 3: GET ingress/device/config (nozzle config) ──────────────
        cfg.url = "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/ingress/core/v1/device/config";;
        if (pal_http_client_init(&cfg, &h) != 0) {
            LOG_MSG_ERROR(CSP_LOG_EN, "req3 init failed");
            ret = CS_ERROR_GET_NOZZLE_CONFIG_FAILED;
            break;
        }

        char basic_auth[512] = {};
        snprintf(basic_auth, sizeof(basic_auth), "Basic %s", _cs_net_cfg.basic_authentication_base64);
        pal_http_client_set_header(h, "Authorization", basic_auth);

        sc = pal_http_client_get(h, &resp);

        bool nozzles_ok = false;
        if (sc == 200 || sc == 201) {
            JsonDocument doc;
            deserializeJson(doc, resp.body, DeserializationOption::NestingLimit(20));
            if (doc.containsKey("data")) {
                JsonObject data    = doc["data"].as<JsonObject>();
                JsonArray  nozzles = data["nozzles"].as<JsonArray>();
                int        n       = (int)nozzles.size();
                if (n > CS_NO_NOZZLES) n = CS_NO_NOZZLES;

                nozzles_ok = true;
                memset(_cs_nozzles, 0, sizeof(_cs_nozzles));
                for (int i = 0; i < n; i++) {
                    JsonObject nz = nozzles[i].as<JsonObject>();
                    const char *nz_uuid  = nz["device_id"]    | (const char *)nullptr;
                    const char *nz_ft    = nz["fuel_type"]     | (const char *)nullptr;
                    const char *nz_ft_s  = nz["fuel_type_str"] | (const char *)nullptr;
                    const char *nz_id    = nz["id"]            | (const char *)nullptr;
                    if (!nz_uuid || !nz_ft || !nz_ft_s || !nz_id) {
                        nozzles_ok = false;
                        break;
                    }
                    strncpy(_cs_nozzles[i].uuid,          nz_uuid, CS_SIZE_UUID         - 1);
                    strncpy(_cs_nozzles[i].fuel_type,     nz_ft,   CS_SIZE_FUEL_TYPE    - 1);
                    strncpy(_cs_nozzles[i].fuel_type_str, nz_ft_s, CS_SIZE_FUEL_TYPE_STR- 1);
                    strncpy(_cs_nozzles[i].nozzle_id,     nz_id,   CS_SIZE_NOZZLE_ID    - 1);
                    LOG_MSG_DEBUG(CSP_LOG_EN, "nozzle[%d] uuid=%s ft=%s id=%s",
                                  i, _cs_nozzles[i].uuid, _cs_nozzles[i].fuel_type, _cs_nozzles[i].nozzle_id);
                }
            }
        } else {
            pal_http_response_free(&resp);
            pal_http_client_cleanup(h);
            ret = CS_ERROR_GET_NOZZLE_CONFIG_FAILED;
            break;
        }

        pal_http_response_free(&resp);
        pal_http_client_cleanup(h);
        h = nullptr;

        if (nozzles_ok) {
            LOG_MSG_INFO(CSP_LOG_EN, "registration complete — nozzles configured");
            ret = CS_ERROR_OK;
        } else {
            LOG_MSG_ERROR(CSP_LOG_EN, "nozzle config incomplete");
            ret = CS_ERROR_GET_NOZZLE_CONFIG_FAILED;
        }

    } while (false);

    return ret;
}

int32_t ModuleCubeSphere::_cs_send_event(const char *json_payload)
{
    pal_http_client_config_t cfg = {};
    cfg.url        = "https://fuel-iot-core-v2-alw5epn3aq-el.a.run.app/api/ingress/core/v1/device/event";;
    cfg.cert_pem   = _cloud_root_ca;
    cfg.timeout_ms = 10000;
    cfg.keep_alive = false;

    pal_http_client_handle_t h = nullptr;
    if (pal_http_client_init(&cfg, &h) != 0) {
        LOG_MSG_ERROR(CSP_LOG_EN, "send_event: init failed");
        return -1;
    }

    char auth[512] = {};
    snprintf(auth, sizeof(auth), "Basic %s", _cs_net_cfg.basic_authentication_base64);
    pal_http_client_set_header(h, "Authorization", auth);
    pal_http_client_set_header(h, "Content-Type", "application/json");

    pal_http_response_t resp = {};
    int32_t sc = pal_http_client_post(h, json_payload, strlen(json_payload), &resp);

    int32_t ret = -1;
    if (sc == 200 || sc == 201) {
        JsonDocument doc;
        deserializeJson(doc, resp.body, DeserializationOption::NestingLimit(20));
        if (doc.containsKey("data")) {
            JsonArray arr = doc["data"].as<JsonArray>();
            JsonObject r0 = arr[0];
            const char *status = r0["status"] | "";
            ret = (strcmp(status, "OK") == 0) ? CS_ERROR_OK : -1;
        }
    } else {
        LOG_MSG_ERROR(CSP_LOG_EN, "send_event HTTP %d", sc);
    }

    pal_http_response_free(&resp);
    pal_http_client_cleanup(h);
    return ret;
}

int32_t ModuleCubeSphere::_cs_send_hb(const cs_hb_info_t &hb)
{
    static char json[2048];
    struct timeval now;
    gettimeofday(&now, nullptr);
    char ts[64] = {};
    _cs_format_iso8601(now.tv_sec + (int)(3600 * 5.5), "+05:30", ts, sizeof(ts));

    JsonDocument doc;
    JsonObject   root   = doc.add<JsonObject>();
    JsonArray    events = root["events"].to<JsonArray>();

    // Main device heartbeat
    JsonObject e0 = events.add<JsonObject>();
    e0["device"] = _cs_net_cfg.agent_uuid;
    e0["time"]   = ts;
    e0["event"]  = "core/heartbeat";
    JsonObject b0 = e0["body"].to<JsonObject>();
    b0["rssi"]   = hb.rssi;
    b0["uptime"] = hb.uptime_sec;

    // Per-nozzle heartbeat
    for (int i = 0; i < CS_NO_NOZZLES; i++) {
        if (_cs_nozzles[i].uuid[0] == '\0') continue;
        JsonObject en = events.add<JsonObject>();
        en["device"] = _cs_nozzles[i].uuid;
        en["time"]   = ts;
        en["event"]  = "core/heartbeat";
        JsonObject bn = en["body"].to<JsonObject>();
        bn["rssi"]   = hb.rssi;
        bn["uptime"] = hb.uptime_sec;
    }

    serializeJson(doc, json, sizeof(json));
    return _cs_send_event(json);
}

int32_t ModuleCubeSphere::_cs_send_startup(const cs_startup_info_t &info)
{
    static char json[2048];
    struct timeval now;
    gettimeofday(&now, nullptr);
    char ts[64] = {};
    _cs_format_iso8601(now.tv_sec + (int)(3600 * 5.5), "+05:30", ts, sizeof(ts));

    JsonDocument doc;
    JsonObject   root   = doc.add<JsonObject>();
    JsonArray    events = root["events"].to<JsonArray>();
    JsonObject   e0     = events.add<JsonObject>();

    e0["device"] = _cs_net_cfg.agent_uuid;
    e0["time"]   = ts;
    e0["event"]  = "core/startup";
    JsonObject body = e0["body"].to<JsonObject>();
    body["hw_type"]       = info.device_type;
    body["hw_version"]    = info.board_version;
    body["sw_version"]    = info.fw_version;
    body["local_ip"]      = info.ip_address;
    body["mac"]           = info.mac_address_str;
    body["wifi_ssid"]     = info.ssid;
    body["wifi_password"] = info.password;
    body["sd_status"]     = info.sd_card_status;

    serializeJson(doc, json, sizeof(json));
    return _cs_send_event(json);
}

int32_t ModuleCubeSphere::_cs_send_reconnect(const cs_reconnect_info_t &r)
{
    static char json[2048];
    struct timeval now;
    gettimeofday(&now, nullptr);
    char ts[64] = {};
    _cs_format_iso8601(now.tv_sec + (int)(3600 * 5.5), "+05:30", ts, sizeof(ts));

    JsonDocument doc;
    JsonObject   root   = doc.add<JsonObject>();
    JsonArray    events = root["events"].to<JsonArray>();
    JsonObject   e0     = events.add<JsonObject>();

    e0["device"] = _cs_net_cfg.agent_uuid;
    e0["time"]   = ts;
    e0["event"]  = "core/reconnect";
    JsonObject body = e0["body"].to<JsonObject>();
    body["rssi"]          = r.rssi;
    body["uptime"]        = r.uptime_sec;
    body["local_ip"]      = r.ip_address;
    body["wifi_ssid"]     = r.ssid;
    body["wifi_password"] = r.password;

    serializeJson(doc, json, sizeof(json));
    return _cs_send_event(json);
}

int32_t ModuleCubeSphere::_cs_send_pumped(const cs_pumped_event_t &ev)
{
    if (ev.nozzle_idx >= CS_NO_NOZZLES) {
        LOG_MSG_ERROR(CSP_LOG_EN, "invalid nozzle_idx %u", ev.nozzle_idx);
        return -1;
    }

    static char json[2048];
    struct timeval now;
    gettimeofday(&now, nullptr);
    char ts[64] = {};
    _cs_format_iso8601(now.tv_sec + (int)(3600 * 5.5), "+05:30", ts, sizeof(ts));

    JsonDocument doc;
    JsonObject   root   = doc.add<JsonObject>();
    JsonArray    events = root["events"].to<JsonArray>();
    JsonObject   e0     = events.add<JsonObject>();

    e0["device"] = _cs_nozzles[ev.nozzle_idx].uuid;
    e0["time"]   = ts;
    e0["event"]  = "app.fuel/pump-end";
    JsonObject body = e0["body"].to<JsonObject>();
    body["L"]  = ev.volume_lx1000  * 0.001;
    body["T"]  = _cs_nozzles[ev.nozzle_idx].fuel_type;
    body["P"]  = ev.total_pricex100 * 0.01;
    body["U"]  = ev.unit_pricex100  * 0.01;
    body["ID"] = ev.event_id;

    serializeJson(doc, json, sizeof(json));
    LOG_MSG_DEBUG(CSP_LOG_EN, "pumped json: %s", json);
    return _cs_send_event(json);
}

int32_t ModuleCubeSphere::_cs_send_status_updated(const cs_startup_info_t &info)
{
    static char json[2048];
    struct timeval now;
    gettimeofday(&now, nullptr);
    char ts[64] = {};
    _cs_format_iso8601(now.tv_sec + (int)(3600 * 5.5), "+05:30", ts, sizeof(ts));

    JsonDocument doc;
    JsonObject   root   = doc.add<JsonObject>();
    JsonArray    events = root["events"].to<JsonArray>();
    JsonObject   e0     = events.add<JsonObject>();

    e0["device"] = _cs_net_cfg.agent_uuid;
    e0["time"]   = ts;
    e0["event"]  = "core/status-updated";
    JsonObject body = e0["body"].to<JsonObject>();
    body["hw_type"]       = info.device_type;
    body["hw_version"]    = info.board_version;
    body["sw_version"]    = info.fw_version;
    body["local_ip"]      = info.ip_address;
    body["mac"]           = info.mac_address_str;
    body["wifi_ssid"]     = info.ssid;
    body["wifi_password"] = info.password;
    body["sd_status"]     = info.sd_card_status;

    serializeJson(doc, json, sizeof(json));
    return _cs_send_event(json);
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
    cfg.list_config.parent_path        = "/sd/retx";
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

    if (retx_mgr_init(&_retx_mgr, &cfg) == RETX_MGR_OK) {
        _retx_ready = true;
        retx_mgr_cleanup(&_retx_mgr);
        LOG_MSG_INFO(CSP_LOG_EN, "retx: ready");
    } else {
        LOG_MSG_ERROR(CSP_LOG_EN, "retx: init failed");
    }
}

void ModuleCubeSphere::_retx_store_pumped(const MsgFuelPumped::Payload &p,
                                           const char * /*json_payload*/)
{
    if (!_retx_ready) {
        LOG_MSG_WARNING(CSP_LOG_EN, "retx: not ready — event lost");
        return;
    }

    char json[256];
    snprintf(json, sizeof(json),
             "{\"nozzle\":%u,\"vol\":%lu,\"unit\":%lu,\"total\":%lu,\"ts\":%llu}",
             (unsigned)p.nozzle_idx, (unsigned long)p.vol_lx1000,
             (unsigned long)p.unit_pricex100, (unsigned long)p.total_pricex100,
             (unsigned long long)time(nullptr));

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
