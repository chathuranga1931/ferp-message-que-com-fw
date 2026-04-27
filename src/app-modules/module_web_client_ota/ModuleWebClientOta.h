// ModuleWebClientOta.h
//
// ModuleWebClientOta — cloud-polling OTA source module.
//
// This module periodically polls the OTA server for firmware updates across
// all configured targets.  When an update is detected it participates in the
// standard HSYS OTA session protocol as a SOURCE:
//
//   1. Send MsgOtaStartRequest  → OtaModule  (request session for target_idx)
//   2. Recv MsgOtaStartResponse ← OtaModule  (ACCEPTED or REJECTED)
//   3. Send MsgOtaRequestDriver → OtaModule
//   4. Recv MsgOtaDriverResponse← OtaModule  (ota_fs_driver_t + ctx)
//   5. Call hsys_ota_download_to_fs() — blocking HTTP stream into driver
//   6. Publish MsgOtaProgress    (during download, per chunk)
//   7. Send MsgOtaCompleteNotify → OtaModule  (success / failure)
//
// Blocking note:
//   Steps 1b (version check) and 5 (download) are blocking HTTP calls.
//   The module must run in a DEDICATED task (web_ota_task) so that blocking
//   does not stall any other module.
//
// Configuration:
//   Construct with a static array of web_ota_target_cfg_t descriptors.
//   The server URL, device UUID, and root CA are read from app_config at
//   runtime (after MsgConfigCloud is received).

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "hsys_ota.h"           /* hsys_ota_cfg_t, hsys_ota_check_result_t */

// ── Module identity ────────────────────────────────────────────────────────

#define MODULE_WEB_CLIENT_OTA_NAME  "WBCOTMOD"   /* exactly 8 chars */

// ── Per-target poll configuration ─────────────────────────────────────────

/**
 * @brief Describes one firmware target to poll for updates.
 *
 * Allocate as a static array and pass to the constructor.  The strings must
 * have static storage duration.
 */
typedef struct {
    uint8_t     target_idx;       ///< Matches ota_target_desc_t.target_idx in app_ota_config.h
    const char *firmware_type;    ///< Sent to the server, e.g. "ferp-esp32-main"
    const char *current_version;  ///< Currently installed version, e.g. "1.0.0"
} web_ota_target_cfg_t;

// ── Poll-interval limits ──────────────────────────────────────────────────

/** Minimum allowed polling interval (seconds). */
#define WEB_OTA_CHECK_INTERVAL_MIN_S   30u    /* 30 seconds — minimum for testing */

/** Maximum allowed polling interval (seconds). */
#define WEB_OTA_CHECK_INTERVAL_MAX_S   300u   /* 5 minutes */

// ── ModuleWebClientOta ─────────────────────────────────────────────────────

class ModuleWebClientOta : public HsysModule
{
public:
    /**
     * @param targets      Static array of target descriptors (static lifetime).
     * @param target_count Number of entries in @p targets.
     */
    ModuleWebClientOta(const web_ota_target_cfg_t *targets, uint8_t target_count);

    static ModuleWebClientOta *instance();

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    // ── Configuration ──────────────────────────────────────────────────────
    const web_ota_target_cfg_t *_targets      = nullptr;
    uint8_t                     _target_count = 0;

    // ── Runtime state ──────────────────────────────────────────────────────

    /** Internal state machine states. */
    typedef enum {
        STATE_IDLE,              ///< Waiting for internet + next tick period
        STATE_REQUESTING_SESSION,///< MsgOtaStartRequest sent; waiting for response
        STATE_REQUESTING_DRIVER, ///< MsgOtaRequestDriver sent; waiting for response
        STATE_DOWNLOADING,       ///< hsys_ota_download_to_fs() running (blocking)
    } state_t;

    state_t  _state          = STATE_IDLE;
    bool     _internet_ok       = false;   ///< Set by MsgInternetStatus
    bool     _config_ready      = false;   ///< Set when MsgConfigOta received (URL + cert)
    bool     _cloud_registered  = false;   ///< Set when MsgCloudStatus(REGISTERED) received (UUID)
    uint32_t _check_interval_s  = WEB_OTA_CHECK_INTERVAL_MIN_S; ///< Current polling interval (seconds)
    uint32_t _tick_count        = 0;       ///< Counts MsgTick1000ms events
    uint8_t  _next_slot         = 0;       ///< Next target slot index to check

    /** Index into _targets[] for the active session. */
    uint8_t  _active_slot    = 0;

    /** Version string returned by the last successful version check. */
    char     _pending_version[HSYS_OTA_MAX_VERSION_LEN];

    // ── Cached config (populated from two sources) ───────────────────────
    // _server_url + _cert_pem : from MsgConfigOta (sent by ModuleConfig)
    // _device_id              : from MsgCloudStatus(REGISTERED) (sent by ModuleCloud)
    char        _server_url[HSYS_OTA_MAX_URL_LEN]      = {};
    char        _device_id [HSYS_OTA_MAX_DEVICE_ID_LEN]= {};
    const char *_cert_pem  = nullptr;

    // ── Internal helpers ───────────────────────────────────────────────────

    /**
     * Build an hsys_ota_cfg_t for the given target slot using cached config.
     * Returns false if config is not yet ready.
     */
    bool _build_cfg(uint8_t slot, hsys_ota_cfg_t *out) const;

    /**
     * Perform a version check for the given slot.
     * On success with update_available, initiates the OTA session.
     * On no-update, increments _next_slot and resets to IDLE.
     */
    void _check_target(uint8_t slot);

    // ── Download progress bridge ───────────────────────────────────────────

    /**
     * Static callback passed to hsys_ota_download_to_fs().
     * @p arg is a pointer to this ModuleWebClientOta instance.
     */
    static void _s_progress_cb(uint32_t bytes, uint32_t total,
                                uint8_t pct, void *arg);

    /** Called from _s_progress_cb — publishes MsgOtaProgress on the bus. */
    void _publish_progress(uint32_t bytes, uint32_t total, uint8_t pct);
};
