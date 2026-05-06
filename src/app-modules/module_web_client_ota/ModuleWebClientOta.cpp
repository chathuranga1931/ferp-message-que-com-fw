// ModuleWebClientOta.cpp
//
// ModuleWebClientOta — cloud-polling OTA source module.
//
// All HTTP calls are message-driven through ModuleHttp — no blocking.
// See ModuleWebClientOta.h for the full protocol description.

#include "ModuleWebClientOta.h"

#include "msg_config_ready.h"
#include "msg_config_get_ota.h"
#include "msg_config_ota.h"
#include "msg_cubesphere_status.h"
#include "msg_internet_status.h"
#include "msg_tick_1000ms.h"
#include "msg_ota_start_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_request_driver.h"
#include "msg_ota_driver_response.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_progress.h"

// HTTP session messages
#include "msg_http_start_request.h"
#include "msg_http_start_response.h"
#include "msg_http_set_url_request.h"
#include "msg_http_set_root_ca_request.h"
#include "msg_http_header_request.h"
#include "msg_http_body_request.h"
#include "msg_http_send_request.h"
#include "msg_http_result.h"
#include "msg_http_set_stream_sink.h"

#include "pal_logger.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

#define __TAG__     "WBCOTMOD"
#define LOG_EN      true

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleWebClientOta *s_instance = nullptr;

ModuleWebClientOta *ModuleWebClientOta::instance()
{
    return s_instance;
}

// ── Constructor ───────────────────────────────────────────────────────────────

ModuleWebClientOta::ModuleWebClientOta(const web_ota_target_cfg_t *targets,
                                       uint8_t                     target_count)
    : HsysModule(MODULE_WEB_CLIENT_OTA_ID, MODULE_WEB_CLIENT_OTA_NAME)
    , _targets(targets)
    , _target_count(target_count)
{
    s_instance = this;
}

// ── init ──────────────────────────────────────────────────────────────────────

void ModuleWebClientOta::init()
{
    subscribe(MsgConfigReady::ID);
    subscribe(MsgConfigOta::ID);
    subscribe(MsgCubesphereStatus::ID);
    subscribe(MsgInternetStatus::ID);
    subscribe(MsgTick1000ms::ID);
    subscribe(MsgOtaStartResponse::ID);
    subscribe(MsgOtaDriverResponse::ID);
    subscribe(MsgHttpStartResponse::ID);
    subscribe(MsgHttpResult::ID);

    LOG_MSG_INFO(LOG_EN, "init  targets=%u  interval=%u..%us (default=%us)",
                 (unsigned)_target_count,
                 (unsigned)WEB_OTA_CHECK_INTERVAL_MIN_S,
                 (unsigned)WEB_OTA_CHECK_INTERVAL_MAX_S,
                 (unsigned)_check_interval_s);
}

// ── _build_cfg ────────────────────────────────────────────────────────────────

bool ModuleWebClientOta::_build_cfg(uint8_t slot, hsys_ota_cfg_t *out) const
{
    if (!_config_ready || slot >= _target_count) return false;

    memset(out, 0, sizeof(*out));
    strncpy(out->server_url,      _server_url,                        sizeof(out->server_url)      - 1);
    strncpy(out->device_id,       _device_id,                         sizeof(out->device_id)       - 1);
    strncpy(out->firmware_type,   _targets[slot].firmware_type,       sizeof(out->firmware_type)   - 1);
    strncpy(out->current_version, _targets[slot].current_version,     sizeof(out->current_version) - 1);
    out->timeout_ms = 30000;
    out->cert_pem   = _cert_pem;
    return true;
}

// ── HTTP session helpers ──────────────────────────────────────────────────────

void ModuleWebClientOta::_send_http_start(pal_http_method_t method, uint32_t timeout_ms)
{
    MsgHttpStartRequest::Payload p{};
    p.method     = method;
    p.timeout_ms = timeout_ms;
    hsys_msg_t *m = MsgHttpStartRequest::create(id(), p);
    if (m) {
        send(m, MODULE_HTTP_ID);
        _http_phase = HTTP_STARTING;
    } else {
        LOG_MSG_ERROR(LOG_EN, "_send_http_start: pool full");
    }
}

void ModuleWebClientOta::_burst_check_post(uint8_t slot)
{
    hsys_ota_cfg_t cfg;
    if (!_build_cfg(slot, &cfg)) {
        LOG_MSG_ERROR(LOG_EN, "_burst_check_post: cfg not ready");
        _state      = STATE_IDLE;
        _http_phase = HTTP_IDLE;
        return;
    }

    char url[HSYS_OTA_MAX_URL_LEN + 64];
    snprintf(url, sizeof(url), "%s/api/v1/firmware/check", cfg.server_url);

    char body[320];
    snprintf(body, sizeof(body),
             "{\"device_id\":\"%s\",\"current_version\":\"%s\",\"firmware_type\":\"%s\"}",
             cfg.device_id, cfg.current_version, cfg.firmware_type);

    { MsgHttpSetRootCaRequest::Payload rca{}; rca.cert_pem = cfg.cert_pem;
      hsys_msg_t *m = MsgHttpSetRootCaRequest::create(id(), rca); if (m) send(m, MODULE_HTTP_ID); }
    { hsys_msg_t *m = MsgHttpSetUrlRequest::create(id(), url); if (m) send(m, MODULE_HTTP_ID); }
    { hsys_msg_t *m = MsgHttpHeaderRequest::create(id(), "Content-Type", "application/json");
      if (m) send(m, MODULE_HTTP_ID); }
    { hsys_msg_t *m = MsgHttpBodyRequest::create(id(), body, (uint32_t)strlen(body));
      if (m) send(m, MODULE_HTTP_ID); }
    { hsys_msg_t *m = MsgHttpSendRequest::create(id()); if (m) send(m, MODULE_HTTP_ID); }

    _http_phase = HTTP_EXECUTING;

    LOG_MSG_INFO(LOG_EN, "check slot=%u  fw=%s  cur=%s",
                 (unsigned)slot, cfg.firmware_type, cfg.current_version);
}

void ModuleWebClientOta::_burst_download_get(uint8_t slot,
                                               const ota_fs_driver_t *drv, void *ctx,
                                               uint32_t expected_crc32)
{
    hsys_ota_cfg_t cfg;
    if (!_build_cfg(slot, &cfg)) {
        LOG_MSG_ERROR(LOG_EN, "_burst_download_get: cfg not ready");
        _state      = STATE_IDLE;
        _http_phase = HTTP_IDLE;
        return;
    }

    char url[HSYS_OTA_MAX_URL_LEN + 192];
    snprintf(url, sizeof(url),
             "%s/api/v1/firmware/download?firmware_type=%s&version=%s&device_id=%s",
             cfg.server_url, cfg.firmware_type, _pending_version, cfg.device_id);

    { MsgHttpSetRootCaRequest::Payload rca{}; rca.cert_pem = cfg.cert_pem;
      hsys_msg_t *m = MsgHttpSetRootCaRequest::create(id(), rca); if (m) send(m, MODULE_HTTP_ID); }
    { MsgHttpSetStreamSink::Payload ss{}; ss.fs_drv = drv; ss.fs_ctx = ctx;
      ss.expected_crc32 = expected_crc32;
      hsys_msg_t *m = MsgHttpSetStreamSink::create(id(), ss); if (m) send(m, MODULE_HTTP_ID); }
    { hsys_msg_t *m = MsgHttpSetUrlRequest::create(id(), url); if (m) send(m, MODULE_HTTP_ID); }
    { hsys_msg_t *m = MsgHttpSendRequest::create(id()); if (m) send(m, MODULE_HTTP_ID); }

    _http_phase = HTTP_EXECUTING;

    LOG_MSG_INFO(LOG_EN, "download slot=%u  fw=%s  ver=%s  crc=0x%08lX",
                 (unsigned)slot, cfg.firmware_type, _pending_version,
                 (unsigned long)expected_crc32);
}

// ── HTTP response handlers ────────────────────────────────────────────────────

void ModuleWebClientOta::_on_http_start_response(const hsys_msg_t &msg)
{
    if (_http_phase != HTTP_STARTING) return;

    auto p = MsgHttpStartResponse::deserialize(msg);

    if (p.result == HTTP_SESSION_BUSY) {
        LOG_MSG_WARNING(LOG_EN, "HTTP session busy — will retry next interval");
        _http_phase = HTTP_IDLE;
        if (_state == STATE_HTTP_CHECK) {
            _state = STATE_IDLE;  // re-trigger on next tick
        } else if (_state == STATE_HTTP_DOWNLOAD) {
            if (_dl_drv) _dl_drv->ferase(_dl_ctx);
            MsgOtaCompleteNotify::Payload n{};
            n.success    = false;
            n.last_error = OTA_FS_ERR_WRITE_FAIL;
            hsys_msg_t *nm = MsgOtaCompleteNotify::create(id(), n);
            if (nm) send(nm, MODULE_OTA_ID);
            _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
            _state     = STATE_IDLE;
        }
        return;
    }

    if (_state == STATE_HTTP_CHECK) {
        _burst_check_post(_active_slot);
    } else if (_state == STATE_HTTP_DOWNLOAD) {
        _burst_download_get(_active_slot, _dl_drv, _dl_ctx, _pending_crc32);
    }
}

void ModuleWebClientOta::_on_check_result(const hsys_msg_t &msg)
{
    _http_phase = HTTP_IDLE;

    auto f = MsgHttpResult::get_fields(msg);

    if (f.result != HTTP_RESULT_SUCCESS || f.status_code != 200 ||
        !f.body || f.body_len == 0) {
        LOG_MSG_ERROR(LOG_EN, "check failed  result=%d  status=%d  slot=%u",
                      (int)f.result, (int)f.status_code, (unsigned)_active_slot);
        _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
        _state     = STATE_IDLE;
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, (const char *)f.body, f.body_len);
    if (err) {
        LOG_MSG_ERROR(LOG_EN, "check JSON error: %s  slot=%u", err.c_str(), (unsigned)_active_slot);
        _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
        _state     = STATE_IDLE;
        return;
    }

    bool update_available = doc["update_available"] | false;
    if (!update_available) {
        LOG_MSG_INFO(LOG_EN, "slot=%u  fw=%s  up to date",
                     (unsigned)_active_slot, _targets[_active_slot].firmware_type);
        _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
        _state     = STATE_IDLE;
        return;
    }

    const char *latest = doc["latest_version"] | "";
    strncpy(_pending_version, latest, sizeof(_pending_version) - 1);

    const char *crc_str = doc["crc32"] | "0x00000000";
    _pending_crc32 = (uint32_t)strtoul(crc_str, nullptr, 16);
    uint32_t file_size = doc["file_size"] | 0;

    LOG_MSG_INFO(LOG_EN, "slot=%u  update: %s → %s  %lu bytes  CRC=0x%08lX",
                 (unsigned)_active_slot,
                 _targets[_active_slot].current_version,
                 _pending_version,
                 (unsigned long)file_size,
                 (unsigned long)_pending_crc32);

    MsgOtaStartRequest::Payload req{};
    req.target_idx = _targets[_active_slot].target_idx;
    strncpy(req.incoming_version, _pending_version, sizeof(req.incoming_version) - 1);

    hsys_msg_t *m = MsgOtaStartRequest::create(id(), req);
    if (m) {
        send(m, MODULE_OTA_ID);
        _state = STATE_REQUESTING_SESSION;
    } else {
        LOG_MSG_ERROR(LOG_EN, "pool full — can't send StartRequest");
        _state = STATE_IDLE;
    }
}

void ModuleWebClientOta::_on_download_result(const hsys_msg_t &msg)
{
    _http_phase = HTTP_IDLE;

    auto f    = MsgHttpResult::get_fields(msg);
    bool ok   = (f.result == HTTP_RESULT_SUCCESS);

    MsgOtaCompleteNotify::Payload notify{};
    notify.success    = ok;
    notify.last_error = ok ? OTA_FS_OK : OTA_FS_ERR_WRITE_FAIL;

    hsys_msg_t *nm = MsgOtaCompleteNotify::create(id(), notify);
    if (nm) send(nm, MODULE_OTA_ID);

    if (ok) {
        LOG_MSG_INFO(LOG_EN, "download complete  slot=%u  fw=%s  ver=%s",
                     (unsigned)_active_slot, _targets[_active_slot].firmware_type,
                     _pending_version);
    } else {
        LOG_MSG_ERROR(LOG_EN, "download failed  slot=%u  result=%d  status=%d",
                      (unsigned)_active_slot, (int)f.result, (int)f.status_code);
    }

    _dl_drv    = nullptr;
    _dl_ctx    = nullptr;
    _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
    _state     = STATE_IDLE;
}

// ── Progress publish ──────────────────────────────────────────────────────────

void ModuleWebClientOta::_publish_progress(uint32_t bytes, uint32_t total, uint8_t pct)
{
    MsgOtaProgress::Payload p{};
    p.target_idx    = _targets[_active_slot].target_idx;
    p.percent       = pct;
    p.bytes_written = bytes;
    p.total_bytes   = total;

    hsys_msg_t *m = MsgOtaProgress::create(id(), p);
    if (m) publish(m);
}

// ── on_msg_received ───────────────────────────────────────────────────────────

void ModuleWebClientOta::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {

    /* ── Config ready — request OTA config ──────────────────────────── */
    case MsgConfigReady::ID: {
        MsgConfigGetOta::Payload gcp = { id(), {} };
        hsys_msg_t *req = MsgConfigGetOta::create(id(), gcp);
        if (req) send(req, MODULE_CONFIG_ID);
        break;
    }

    /* ── OTA config arrived ─────────────────────────────────────────── */
    case MsgConfigOta::ID: {
        auto p = MsgConfigOta::deserialize(msg);
        strncpy(_server_url, p.server_url, sizeof(_server_url) - 1);
        _cert_pem = p.root_ca;

        uint32_t raw = p.check_interval_s;
        if (raw < WEB_OTA_CHECK_INTERVAL_MIN_S) raw = WEB_OTA_CHECK_INTERVAL_MIN_S;
        if (raw > WEB_OTA_CHECK_INTERVAL_MAX_S) raw = WEB_OTA_CHECK_INTERVAL_MAX_S;
        _check_interval_s = raw;

        _config_ready = true;
        LOG_MSG_INFO(LOG_EN, "config ready  url=%s  interval=%us", _server_url,
                     (unsigned)_check_interval_s);
        break;
    }

    /* ── Cloud status — capture device UUID when registered ─────────── */
    case MsgCubesphereStatus::ID: {
        auto p = MsgCubesphereStatus::deserialize(msg);
        if (p.event == CUBESPHERE_STATUS_REGISTERED) {
            strncpy(_device_id, p.device_uuid, sizeof(_device_id) - 1);
            _cloud_registered = true;
            LOG_MSG_INFO(LOG_EN, "cloud registered  uuid=%s", _device_id);
        }
        break;
    }

    /* ── Internet status ─────────────────────────────────────────────── */
    case MsgInternetStatus::ID: {
        auto p       = MsgInternetStatus::deserialize(msg);
        _internet_ok = p.connected;
        if (!_internet_ok && _state != STATE_IDLE) {
            LOG_MSG_INFO(LOG_EN, "internet lost — aborting  state=%d", (int)_state);
            _state      = STATE_IDLE;
            _http_phase = HTTP_IDLE;
        }
        break;
    }

    /* ── Periodic tick — drive the check timer ───────────────────────── */
    case MsgTick1000ms::ID: {
        if (_state != STATE_IDLE)     break;
        if (_http_phase != HTTP_IDLE) break;
        if (!_internet_ok)            break;
        if (!_config_ready)           break;
        if (!_cloud_registered)       break;

        _tick_count++;
        if (_tick_count < _check_interval_s) break;

        _tick_count  = 0;
        _active_slot = _next_slot;
        _state       = STATE_HTTP_CHECK;
        _send_http_start(PAL_HTTP_METHOD_POST, 30000);
        break;
    }

    /* ── HTTP start response ─────────────────────────────────────────── */
    case MsgHttpStartResponse::ID:
        _on_http_start_response(msg);
        break;

    /* ── HTTP result ─────────────────────────────────────────────────── */
    case MsgHttpResult::ID:
        if (_state == STATE_HTTP_CHECK) {
            _on_check_result(msg);
        } else if (_state == STATE_HTTP_DOWNLOAD) {
            _on_download_result(msg);
        }
        break;

    /* ── OTA session accepted / rejected ────────────────────────────── */
    case MsgOtaStartResponse::ID: {
        if (_state != STATE_REQUESTING_SESSION) break;

        auto p = MsgOtaStartResponse::deserialize(msg);
        if (p.result == OTA_START_ACCEPTED) {
            hsys_msg_t *req = MsgOtaRequestDriver::create(id());
            if (req) {
                send(req, MODULE_OTA_ID);
                _state = STATE_REQUESTING_DRIVER;
                LOG_MSG_INFO(LOG_EN, "session accepted  → REQUESTING_DRIVER");
            } else {
                LOG_MSG_ERROR(LOG_EN, "pool full — can't send RequestDriver");
                _state = STATE_IDLE;
            }
        } else {
            LOG_MSG_INFO(LOG_EN, "session rejected result=%d", (int)p.result);
            _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
            _state     = STATE_IDLE;
        }
        break;
    }

    /* ── OTA driver arrived — start message-driven download ─────────── */
    case MsgOtaDriverResponse::ID: {
        if (_state != STATE_REQUESTING_DRIVER) break;

        auto p  = MsgOtaDriverResponse::deserialize(msg);
        _dl_drv = p.driver;
        _dl_ctx = p.ctx;

        LOG_MSG_INFO(LOG_EN, "driver received  slot=%u  ver=%s  → HTTP_DOWNLOAD",
                     (unsigned)_active_slot, _pending_version);

        _state = STATE_HTTP_DOWNLOAD;
        _send_http_start(PAL_HTTP_METHOD_GET, 120000);
        break;
    }

    default:
        break;
    }
}
