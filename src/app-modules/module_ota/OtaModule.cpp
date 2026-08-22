// OtaModule.cpp
//
// ModuleOta — OTA session manager.
// See OtaModule.h for the state machine description.

#include "OtaModule.h"
#include "msg_ota_start_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_request_driver.h"
#include "msg_ota_driver_response.h"
#include "msg_ota_abort_request.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_event.h"
#include "msg_ota_progress.h"
#include "msg_tick_1000ms.h"
#include "msg_mqtt_status.h"
#include "pal_logger.h"
#include "pal_time.h"
#include "pal_power.h"
#include "pal_fw_update.h"
#include <new>
#include <string.h>

#define __TAG__       "MOD_OTA "
#define OTA_LOG_EN    true

// Delay (ms) between publishing OTA_EVENT_COMPLETE and calling pal_power_reset().
// Gives other modules time to react to the event before the system restarts.
#define OTA_REBOOT_DELAY_MS  ((uint64_t)2000U)

// ── Singleton (lazy placement-new) ────────────────────────────────────────────
//
// instance() returns a stable address before the constructor runs.
// Call set_platform_config() in app_init() (before hsys_module_init()) to supply
// the source/target tables.  Both arrays must have static lifetime.

alignas(OtaModule) static char  s_buf[sizeof(OtaModule)];
static OtaModule               *s_ptr = nullptr;

OtaModule *OtaModule::instance()
{
    if (!s_ptr) {
        s_ptr = ::new (s_buf) OtaModule();
    }
    return s_ptr;
}

OtaModule::OtaModule()
    : HsysModule(MODULE_OTA_ID, MODULE_OTA_NAME)
{}

void OtaModule::set_platform_config(const ota_source_desc_t *sources, uint8_t source_count,
                                     const ota_target_desc_t *targets, uint8_t target_count)
{
    _sources      = sources;
    _source_count = source_count;
    _targets      = targets;
    _target_count = target_count;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void OtaModule::init()
{
    subscribe(MsgOtaStartRequest::ID);
    subscribe(MsgOtaRequestDriver::ID);
    subscribe(MsgOtaAbortRequest::ID);
    subscribe(MsgOtaCompleteNotify::ID);
    subscribe(MsgOtaProgress::ID);
    subscribe(MSG_ID_TICK_1000MS);   // used to check inactivity / PENDING deadline
    subscribe(MsgMqttStatus::ID);    // used for one-time post-boot rollback confirmation

    LOG_MSG_INFO(OTA_LOG_EN, "init — state=IDLE sources=%u targets=%u",
                 (unsigned)_source_count, (unsigned)_target_count);
}

// ── Message dispatch ──────────────────────────────────────────────────────────

void OtaModule::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {

        case MsgOtaStartRequest::ID:
            _handle_start_request(msg);
            break;

        case MsgOtaRequestDriver::ID:
            _handle_request_driver(msg);
            break;

        case MsgOtaAbortRequest::ID:
            _handle_abort_request(msg);
            break;

        case MsgOtaCompleteNotify::ID:
            _handle_complete_notify(msg);
            break;

        case MsgOtaProgress::ID:
            _handle_progress(msg);
            break;

        case MSG_ID_TICK_1000MS:
            _handle_tick();
            break;

        case MsgMqttStatus::ID:
            _handle_mqtt_status(msg);
            break;

        default:
            break;
    }
}

// ── Handlers ──────────────────────────────────────────────────────────────────

void OtaModule::_handle_start_request(const hsys_msg_t &msg)
{
    auto p = MsgOtaStartRequest::deserialize(msg);

    // Reject if another session is active
    if (_state != STATE_IDLE) {
        LOG_MSG_INFO(OTA_LOG_EN, "start: busy (state=%d) — rejecting source %u",
                     (int)_state, (unsigned)msg.sender_id);
        _reply_start(msg.sender_id, OTA_START_REJECTED_BUSY);
        return;
    }

    // Validate source
    const ota_source_desc_t *src = _find_source(msg.sender_id);
    if (!src) {
        LOG_MSG_INFO(OTA_LOG_EN, "start: unknown source %u", (unsigned)msg.sender_id);
        _reply_start(msg.sender_id, OTA_START_REJECTED_UNKNOWN_SOURCE);
        return;
    }

    // Validate target
    const ota_target_desc_t *tgt = _find_target(p.target_idx);
    if (!tgt) {
        LOG_MSG_INFO(OTA_LOG_EN, "start: unknown target idx %u", (unsigned)p.target_idx);
        _reply_start(msg.sender_id, OTA_START_REJECTED_UNKNOWN_TARGET);
        return;
    }

    // Accept the session — enter PENDING
    _active_source_id   = msg.sender_id;
    _active_target      = tgt;
    _timeout_ms         = src->timeout_ms;
    _session_start_ts   = pal_time_get_ms();
    _last_progress_ts   = _session_start_ts;
    memset(_active_version, 0, sizeof(_active_version));
    strncpy(_active_version, p.incoming_version, sizeof(_active_version) - 1);

    _state = STATE_PENDING;

    LOG_MSG_INFO(OTA_LOG_EN, "start: ACCEPTED src=%u target=%s ver='%s' timeout=%lums",
                 (unsigned)msg.sender_id, tgt->label, _active_version,
                 (unsigned long)_timeout_ms);

    _reply_start(msg.sender_id, OTA_START_ACCEPTED);
}

void OtaModule::_handle_request_driver(const hsys_msg_t &msg)
{
    if (_state != STATE_PENDING) {
        LOG_MSG_INFO(OTA_LOG_EN, "request_driver: not in PENDING (state=%d)", (int)_state);
        return;
    }

    if (msg.sender_id != _active_source_id) {
        LOG_MSG_INFO(OTA_LOG_EN, "request_driver: wrong sender %u (expected %u)",
                     (unsigned)msg.sender_id, (unsigned)_active_source_id);
        return;
    }

    _state            = STATE_ACTIVE;
    _last_progress_ts = pal_time_get_ms();   // arm inactivity timer from now

    LOG_MSG_INFO(OTA_LOG_EN, "request_driver: → ACTIVE target=%s", _active_target->label);

    // Reply with the driver pointer and ctx
    MsgOtaDriverResponse::Payload rp{};
    rp.driver = _active_target->driver;
    rp.ctx    = _active_target->ctx;

    auto *reply = MsgOtaDriverResponse::create(id(), rp);
    if (reply) {
        send(reply, _active_source_id);
    }

    // Publish session-started notification
    _publish_event(OTA_EVENT_SESSION_STARTED);
}

void OtaModule::_handle_abort_request(const hsys_msg_t &msg)
{
    if (_state != STATE_ACTIVE && _state != STATE_PENDING) {
        return;  // nothing active to abort
    }

    if (msg.sender_id != _active_source_id) {
        return;  // not the session owner
    }

    auto p = MsgOtaAbortRequest::deserialize(msg);
    LOG_MSG_INFO(OTA_LOG_EN, "abort: reason=%d", (int)p.reason);
    _enter_aborting("source request");
}

void OtaModule::_handle_complete_notify(const hsys_msg_t &msg)
{
    if (_state != STATE_ACTIVE) {
        return;
    }

    if (msg.sender_id != _active_source_id) {
        return;
    }

    auto p = MsgOtaCompleteNotify::deserialize(msg);

    if (!p.success) {
        LOG_MSG_INFO(OTA_LOG_EN, "complete: source reported failure (err=%d)", (int)p.last_error);
        _enter_aborting("source write error");
        return;
    }

    LOG_MSG_INFO(OTA_LOG_EN, "complete: binary written — entering COMPLETE");
    _enter_complete();
}

void OtaModule::_handle_progress(const hsys_msg_t &msg)
{
    // Only reset the inactivity timer when in ACTIVE state and for the active source.
    if (_state != STATE_ACTIVE) return;
    if (msg.sender_id != _active_source_id) return;

    _last_progress_ts = pal_time_get_ms();
}

void OtaModule::_handle_tick()
{
    uint64_t now = pal_time_get_ms();

    switch (_state) {

        case STATE_PENDING:
        {
            // Fixed deadline: if no MsgOtaRequestDriver within timeout_ms, abort.
            if (_timeout_ms == 0) break;
            if ((now - _session_start_ts) >= _timeout_ms) {
                LOG_MSG_INFO(OTA_LOG_EN, "PENDING deadline exceeded — aborting");
                _publish_event(OTA_EVENT_TIMEOUT);
                _enter_aborting("PENDING timeout");
            }
            break;
        }

        case STATE_ACTIVE:
        {
            // Inactivity timeout: resets on every MsgOtaProgress received.
            if (_timeout_ms == 0) break;
            if ((now - _last_progress_ts) >= _timeout_ms) {
                LOG_MSG_INFO(OTA_LOG_EN, "inactivity timeout — aborting");
                _publish_event(OTA_EVENT_TIMEOUT);
                _enter_aborting("inactivity timeout");
            }
            break;
        }

        case STATE_COMPLETE:
        {
            // Wait the reboot delay, then reset (if target requires it).
            if ((now - _session_start_ts) >= OTA_REBOOT_DELAY_MS) {
                if (_active_target && _active_target->needs_reboot) {
                    LOG_MSG_INFO(OTA_LOG_EN, "rebooting into new firmware");
                    pal_power_reset();
                    // does not return
                }
                // No reboot needed (e.g. esp07 SPIFFS staging) — just return to IDLE
                _reset_session();
            }
            break;
        }

        default:
            break;
    }
}

void OtaModule::_handle_mqtt_status(const hsys_msg_t &msg)
{
    if (_ota_confirmed) return;

    auto p = MsgMqttStatus::deserialize(msg);
    if (!p.connected) return;

    // First successful MQTT connection this boot confirms the running image
    // can actually do its job — cancel any pending OTA rollback so a later,
    // unrelated crash/reset doesn't revert the device to its previous
    // firmware. See pal_fw_update_mark_valid() for why this is needed.
    _ota_confirmed = true;
    pal_fw_update_mark_valid();
}

// ── State transitions ─────────────────────────────────────────────────────────

void OtaModule::_enter_aborting(const char *reason)
{
    _state = STATE_ABORTING;

    // Erase the partial write
    if (_active_target && _active_target->driver && _active_target->driver->ferase) {
        ota_fs_err_t err = _active_target->driver->ferase(_active_target->ctx);
        if (err != OTA_FS_OK) {
            LOG_MSG_INFO(OTA_LOG_EN, "ferase failed: %d", (int)err);
        }
    }

    LOG_MSG_INFO(OTA_LOG_EN, "ABORTING: %s", reason ? reason : "");
    _publish_event(OTA_EVENT_SESSION_ABORTED);
    _reset_session();
}

void OtaModule::_enter_complete()
{
    _state = STATE_COMPLETE;
    _session_start_ts = pal_time_get_ms();   // reuse as reboot-delay start
    _publish_event(OTA_EVENT_COMPLETE);
    LOG_MSG_INFO(OTA_LOG_EN, "COMPLETE — needs_reboot=%d",
                 (int)(_active_target ? _active_target->needs_reboot : 0));
}

void OtaModule::_reset_session()
{
    _state            = STATE_IDLE;
    _active_source_id = 0;
    _active_target    = nullptr;
    _timeout_ms       = 0;
    _last_progress_ts = 0ULL;
    _session_start_ts = 0ULL;
    memset(_active_version, 0, sizeof(_active_version));
    LOG_MSG_INFO(OTA_LOG_EN, "→ IDLE");
}

// ── Helpers ───────────────────────────────────────────────────────────────────

const ota_source_desc_t *OtaModule::_find_source(hsys_module_id_t module_id) const
{
    for (uint8_t i = 0; i < _source_count; i++) {
        if (_sources[i].source_module_id == module_id)
            return &_sources[i];
    }
    return nullptr;
}

const ota_target_desc_t *OtaModule::_find_target(uint8_t target_idx) const
{
    for (uint8_t i = 0; i < _target_count; i++) {
        if (_targets[i].target_idx == target_idx)
            return &_targets[i];
    }
    return nullptr;
}

void OtaModule::_publish_event(ota_event_id_t event)
{
    MsgOtaEvent::Payload p{};
    p.event      = event;
    p.target_idx = _active_target ? _active_target->target_idx : 0xFF;
    strncpy(p.version, _active_version, sizeof(p.version) - 1);

    auto *msg = MsgOtaEvent::create(id(), p);
    if (msg) publish(msg);
}

void OtaModule::_reply_start(hsys_module_id_t target_module, ota_start_result_t result)
{
    MsgOtaStartResponse::Payload p{};
    p.result = result;

    auto *msg = MsgOtaStartResponse::create(id(), p);
    if (msg) send(msg, target_module);
}
