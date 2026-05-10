// OtaModule.h
//
// ModuleOta — OTA session manager.
//
// State machine:
//   IDLE ──[MsgOtaStartRequest from known source]──► PENDING
//   PENDING ──[MsgOtaRequestDriver]──► ACTIVE  (or deadline fires → ABORTING)
//   ACTIVE ──[MsgOtaCompleteNotify(success)]──► COMPLETE
//   ACTIVE ──[MsgOtaAbortRequest | inactivity timeout]──► ABORTING
//   ABORTING ──► IDLE   (after ferase + MsgOtaEvent(SESSION_ABORTED))
//   COMPLETE ──► IDLE   (after MsgOtaEvent(COMPLETE) + pal_power_reset() if needs_reboot)
//
// Binary data NEVER enters the message pool.  Only small control messages
// cross the HSYS bus.  The source calls the ota_fs_driver_t function pointers
// directly with raw binary chunks.
//
// Platform wiring — call set_platform_config() in app_init() before hsys_module_init():
//
//   OtaModule::instance()->set_platform_config(
//       sources, source_count, targets, target_count);
//
// Both arrays must have static lifetime — OtaModule stores only the pointers.
// The call must precede hsys_module_init() so the tables are set before init() runs.

#pragma once

#include "hsys_module.h"
#include "FileSystemDriver.h"
#include "app_module_ids.h"
#include "msg_ota_event.h"
#include "msg_ota_start_response.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Source descriptor — identifies an authorised OTA source module
// ---------------------------------------------------------------------------

typedef struct {
    uint16_t source_module_id; ///< HSYS module ID of the OTA source (hsys_module_id_t)
    uint8_t  priority;         ///< Reserved — not enforced (placeholder for future preemption)
    uint8_t  _pad;
    uint32_t timeout_ms;       ///< Inactivity timeout in ms (resets on MsgOtaProgress); 0 = no timeout
} ota_source_desc_t;

// ---------------------------------------------------------------------------
// Target descriptor — identifies a writable firmware target
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t                target_idx;   ///< Index referenced in MsgOtaStartRequest
    bool                   needs_reboot; ///< true = call pal_power_reset() on COMPLETE
    uint8_t                _pad[2];
    const char            *label;        ///< Human-readable name (e.g. "esp32-main")
    const ota_fs_driver_t *driver;       ///< Driver function table (static lifetime)
    void                  *ctx;          ///< Opaque context passed to every driver call
} ota_target_desc_t;

#define MODULE_OTA_NAME  "OTA     "

// ---------------------------------------------------------------------------
// OtaModule
// ---------------------------------------------------------------------------

class OtaModule : public HsysModule
{
public:
    /**
     * Constructor — both arrays must have static lifetime; OtaModule stores only the pointers.
     * Do not call directly; use instance() which obtains the tables from the platform and
     * passes them here.
     */
    OtaModule();
    static OtaModule *instance();

    /** Supply source/target tables.  Call from app_init() before hsys_module_init().
     *  Both arrays must have static lifetime — OtaModule stores only the pointers. */
    void set_platform_config(const ota_source_desc_t *sources, uint8_t source_count,
                             const ota_target_desc_t *targets, uint8_t target_count);

protected:
    void init()                                 override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    // ── Configuration (set before init) ──────────────────────────────────────
    const ota_source_desc_t *_sources      = nullptr;
    uint8_t                  _source_count = 0;
    const ota_target_desc_t *_targets      = nullptr;
    uint8_t                  _target_count = 0;

    // ── State machine ─────────────────────────────────────────────────────────
    typedef enum {
        STATE_IDLE,
        STATE_PENDING,
        STATE_ACTIVE,
        STATE_ABORTING,
        STATE_COMPLETE,
    } ota_state_t;

    ota_state_t _state = STATE_IDLE;

    // ── Active session context ────────────────────────────────────────────────
    hsys_module_id_t         _active_source_id = 0;   ///< sender_id of the active source
    const ota_target_desc_t *_active_target    = nullptr;
    uint32_t                 _timeout_ms       = 0;   ///< inactivity timeout for active session
    uint64_t                 _last_progress_ts = 0;   ///< timestamp of last MsgOtaProgress (ms)
    uint64_t                 _session_start_ts = 0;   ///< timestamp when PENDING entered (ms)
    char                     _active_version[32] = {};

    // ── Helpers ───────────────────────────────────────────────────────────────
    const ota_source_desc_t *_find_source(hsys_module_id_t module_id) const;
    const ota_target_desc_t *_find_target(uint8_t target_idx) const;

    void _handle_start_request(const hsys_msg_t &msg);
    void _handle_request_driver(const hsys_msg_t &msg);
    void _handle_abort_request(const hsys_msg_t &msg);
    void _handle_complete_notify(const hsys_msg_t &msg);
    void _handle_progress(const hsys_msg_t &msg);
    void _handle_tick();

    void _enter_aborting(const char *reason);
    void _enter_complete();
    void _reset_session();

    void _publish_event(ota_event_id_t event);
    void _reply_start(hsys_module_id_t target_module, ota_start_result_t result);
};
