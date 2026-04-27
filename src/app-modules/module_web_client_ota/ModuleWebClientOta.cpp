// ModuleWebClientOta.cpp
//
// ModuleWebClientOta — cloud-polling OTA source module.
//
// See ModuleWebClientOta.h for the full protocol description.
//
// Blocking note:
//   _check_target() and hsys_ota_download_to_fs() both make blocking HTTP
//   calls.  This is intentional — the module MUST run in its own dedicated
//   task (web_ota_task) so the blocking cannot stall any other module.

#include "ModuleWebClientOta.h"

#include "msg_config_ready.h"
#include "msg_config_get_ota.h"
#include "msg_config_ota.h"
#include "msg_cloud_status.h"
#include "msg_internet_status.h"
#include "msg_tick_1000ms.h"
#include "msg_ota_start_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_request_driver.h"
#include "msg_ota_driver_response.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_progress.h"

#include "pal_logger.h"

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
    subscribe(MsgConfigOta::ID);         /* DIRECT from ModuleConfig */
    subscribe(MsgCloudStatus::ID);       /* NOTIFICATION from ModuleCloud */
    subscribe(MsgInternetStatus::ID);
    subscribe(MsgTick1000ms::ID);
    subscribe(MsgOtaStartResponse::ID);  /* DIRECT from OtaModule */
    subscribe(MsgOtaDriverResponse::ID); /* DIRECT from OtaModule */

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
    strncpy(out->server_url,      _server_url,              sizeof(out->server_url)      - 1);
    strncpy(out->device_id,       _device_id,               sizeof(out->device_id)       - 1);
    strncpy(out->firmware_type,   _targets[slot].firmware_type,
                                                            sizeof(out->firmware_type)    - 1);
    strncpy(out->current_version, _targets[slot].current_version,
                                                            sizeof(out->current_version)  - 1);
    out->timeout_ms = 30000;
    out->cert_pem   = _cert_pem;
    return true;
}

// ── _check_target ─────────────────────────────────────────────────────────────

void ModuleWebClientOta::_check_target(uint8_t slot)
{
    hsys_ota_cfg_t cfg;
    if (!_build_cfg(slot, &cfg)) {
        LOG_MSG_ERROR(LOG_EN, "slot %u: config not ready", (unsigned)slot);
        return;
    }

    LOG_MSG_INFO(LOG_EN, "checking slot %u  fw=%s  cur=%s",
                 (unsigned)slot, cfg.firmware_type, cfg.current_version);

    hsys_ota_check_result_t result;
    int32_t ret = hsys_ota_check_version(&cfg, &result);
    if (ret != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "slot %u: check failed (%ld)", (unsigned)slot, (long)ret);
        /* Advance to next slot; will retry on next timer period */
        _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
        _state     = STATE_IDLE;
        return;
    }

    if (!result.update_available) {
        /* No update — move to next slot */
        _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
        _state     = STATE_IDLE;
        return;
    }

    /* Update found — initiate OTA session */
    strncpy(_pending_version, result.latest_version, sizeof(_pending_version) - 1);
    _active_slot = slot;

    MsgOtaStartRequest::Payload req = {};
    req.target_idx = _targets[slot].target_idx;
    strncpy(req.incoming_version, _pending_version, sizeof(req.incoming_version) - 1);

    hsys_msg_t *msg = MsgOtaStartRequest::create(id(), req);
    if (msg) {
        send(msg, MODULE_OTA_ID);
        _state = STATE_REQUESTING_SESSION;
        LOG_MSG_INFO(LOG_EN, "slot %u: update %s found  target_idx=%u  → REQUESTING_SESSION",
                     (unsigned)slot, _pending_version,
                     (unsigned)_targets[slot].target_idx);
    } else {
        LOG_MSG_ERROR(LOG_EN, "slot %u: pool full — can't send StartRequest", (unsigned)slot);
        _state = STATE_IDLE;
    }
}

// ── Progress callback bridge ──────────────────────────────────────────────────

void ModuleWebClientOta::_s_progress_cb(uint32_t bytes, uint32_t total,
                                         uint8_t pct, void *arg)
{
    if (!arg) return;
    static_cast<ModuleWebClientOta *>(arg)->_publish_progress(bytes, total, pct);
}

void ModuleWebClientOta::_publish_progress(uint32_t bytes, uint32_t total, uint8_t pct)
{
    MsgOtaProgress::Payload p = {};
    p.target_idx    = _targets[_active_slot].target_idx;
    p.percent       = pct;
    p.bytes_written = bytes;
    p.total_bytes   = total;

    hsys_msg_t *msg = MsgOtaProgress::create(id(), p);
    if (msg) publish(msg);
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

    /* ── OTA config arrived — store URL / cert / interval ─────────────── */
    case MsgConfigOta::ID: {
        auto p = MsgConfigOta::deserialize(msg);
        strncpy(_server_url, p.server_url, sizeof(_server_url) - 1);
        _cert_pem = p.root_ca;

        /* Clamp the interval to the allowed range [MIN, MAX] */
        uint32_t raw = p.check_interval_s;
        if (raw < WEB_OTA_CHECK_INTERVAL_MIN_S) raw = WEB_OTA_CHECK_INTERVAL_MIN_S;
        if (raw > WEB_OTA_CHECK_INTERVAL_MAX_S) raw = WEB_OTA_CHECK_INTERVAL_MAX_S;
        _check_interval_s = raw;

        _config_ready = true;
        LOG_MSG_INFO(LOG_EN, "config ready  url=%s  interval=%us", _server_url,
                     (unsigned)_check_interval_s);
        break;
    }

    /* ── Cloud status — wait for REGISTERED to get device UUID ──────── */
    case MsgCloudStatus::ID: {
        auto p = MsgCloudStatus::deserialize(msg);
        if (p.event == CLOUD_STATUS_REGISTERED) {
            strncpy(_device_id, p.device_uuid, sizeof(_device_id) - 1);
            _cloud_registered = true;
            LOG_MSG_INFO(LOG_EN, "cloud registered  uuid=%s  — OTA polling armed",
                         _device_id);
        }
        break;
    }

    /* ── Internet status ─────────────────────────────────────────────── */
    case MsgInternetStatus::ID: {
        auto p        = MsgInternetStatus::deserialize(msg);
        _internet_ok  = p.connected;
        if (!_internet_ok && _state != STATE_IDLE) {
            LOG_MSG_INFO(LOG_EN, "internet lost — aborting  state=%d", (int)_state);
            _state = STATE_IDLE;
        }
        break;
    }

    /* ── Periodic tick — drive the check timer ───────────────────────── */
    case MsgTick1000ms::ID: {
        if (_state != STATE_IDLE) break;    /* session in progress */
        if (!_internet_ok)        break;
        if (!_config_ready)       break;
        if (!_cloud_registered)   break;    /* waiting for cloud UUID */

        _tick_count++;
        if (_tick_count < _check_interval_s) break;

        _tick_count = 0;
        _check_target(_next_slot);
        break;
    }

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
            LOG_MSG_INFO(LOG_EN, "session rejected result=%d  → IDLE", (int)p.result);
            _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
            _state     = STATE_IDLE;
        }
        break;
    }

    /* ── OTA driver arrived — start blocking download ───────────────── */
    case MsgOtaDriverResponse::ID: {
        if (_state != STATE_REQUESTING_DRIVER) break;

        auto p = MsgOtaDriverResponse::deserialize(msg);
        const ota_fs_driver_t *drv = p.driver;
        void                  *ctx = p.ctx;

        _state = STATE_DOWNLOADING;
        LOG_MSG_INFO(LOG_EN, "driver received  slot=%u  ver=%s  → DOWNLOADING",
                     (unsigned)_active_slot, _pending_version);

        /* Build config for download (longer timeout) */
        hsys_ota_cfg_t cfg;
        if (!_build_cfg(_active_slot, &cfg)) {
            LOG_MSG_ERROR(LOG_EN, "cfg build failed");
            MsgOtaCompleteNotify::Payload n = { false, {}, OTA_FS_ERR_INVALID_ARG };
            hsys_msg_t *nm = MsgOtaCompleteNotify::create(id(), n);
            if (nm) send(nm, MODULE_OTA_ID);
            _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
            _state     = STATE_IDLE;
            break;
        }
        cfg.timeout_ms = 120000;   /* generous timeout for large binaries */

        /* ── Blocking download ────────────────────────────────────────── */
        int32_t ret = hsys_ota_download_to_fs(&cfg, _pending_version,
                                               drv, ctx,
                                               _s_progress_cb, this);

        /* ── Notify OtaModule ─────────────────────────────────────────── */
        bool      success  = (ret == PAL_OK);
        ota_fs_err_t fserr = success ? OTA_FS_OK : OTA_FS_ERR_WRITE_FAIL;

        MsgOtaCompleteNotify::Payload notify = {};
        notify.success    = success;
        notify.last_error = fserr;

        hsys_msg_t *nm = MsgOtaCompleteNotify::create(id(), notify);
        if (nm) send(nm, MODULE_OTA_ID);

        if (success) {
            LOG_MSG_INFO(LOG_EN, "download complete  slot=%u  fw=%s  ver=%s",
                         (unsigned)_active_slot,
                         _targets[_active_slot].firmware_type,
                         _pending_version);
        } else {
            LOG_MSG_ERROR(LOG_EN, "download failed  slot=%u  ret=%ld",
                          (unsigned)_active_slot, (long)ret);
        }

        _next_slot = (uint8_t)((_next_slot + 1) % _target_count);
        _state     = STATE_IDLE;
        break;
    }

    default:
        break;
    }
}
