// ModuleWebClientOta.h
//
// ModuleWebClientOta — cloud-polling OTA source module.
//
// This module periodically polls the OTA server for firmware updates across
// all configured targets.  When an update is detected it participates in the
// standard HSYS OTA session protocol as a SOURCE:
//
//   1. Send MsgHttpStartRequest  → ModuleHttp    (POST /check)
//   2. On MsgHttpStartResponse   → burst-send URL/CA/header/body + SendRequest
//   3. On MsgHttpResult          → parse JSON; if update_available:
//   4. Send MsgOtaStartRequest   → OtaModule
//   5. On MsgOtaStartResponse    → if ACCEPTED:
//   6. Send MsgOtaRequestDriver  → OtaModule
//   7. On MsgOtaDriverResponse   → build download URL, send MsgHttpStartRequest
//   8. On MsgHttpStartResponse   → burst-send SetRootCa + SetStreamSink + SetUrl + SendRequest
//   9. On MsgHttpResult          → binary written to fs_driver by ModuleHttp
//  10. Send MsgOtaCompleteNotify → OtaModule
//
// Non-blocking: all HTTP calls go through ModuleHttp via messages.
// This module can share any task with other non-blocking modules.

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "hsys_ota.h"           /* hsys_ota_cfg_t, hsys_ota_check_result_t */
#include "FileSystemDriver.h"   /* ota_fs_driver_t */
#include "http_types.h"         /* http_result_t */
#include "pal_http_client.h"    /* pal_http_method_t */

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

    // ── OTA module state machine ────────────────────────────────────────────

    /** Coarse session state (OTA protocol progress). */
    typedef enum {
        STATE_IDLE,               ///< Waiting for internet + next tick period
        STATE_HTTP_CHECK,         ///< HTTP session open/executing for version check POST
        STATE_REQUESTING_SESSION, ///< MsgOtaStartRequest sent; waiting for response
        STATE_REQUESTING_DRIVER,  ///< MsgOtaRequestDriver sent; waiting for response
        STATE_HTTP_DOWNLOAD,      ///< HTTP session open/executing for firmware download
    } state_t;

    // ── HTTP sub-state ──────────────────────────────────────────────────────

    /** HTTP lifecycle phase within a single HTTP session. */
    typedef enum {
        HTTP_IDLE,       ///< No HTTP session in progress
        HTTP_STARTING,   ///< MsgHttpStartRequest sent; awaiting MsgHttpStartResponse
        HTTP_EXECUTING,  ///< Session open + burst sent; awaiting MsgHttpResult
    } http_phase_t;

    state_t      _state         = STATE_IDLE;
    http_phase_t _http_phase    = HTTP_IDLE;

    bool     _internet_ok       = false;   ///< Set by MsgInternetStatus
    bool     _config_ready      = false;   ///< Set when MsgConfigOta received (URL + cert)
    bool     _cloud_registered  = false;   ///< Set when MsgCloudStatus(REGISTERED) received (UUID)
    uint32_t _check_interval_s  = WEB_OTA_CHECK_INTERVAL_MIN_S; ///< Current polling interval (seconds)
    uint32_t _tick_count        = 0;       ///< Counts MsgTick1000ms events
    uint8_t  _next_slot         = 0;       ///< Next target slot index to check

    /** Index into _targets[] for the active OTA session. */
    uint8_t  _active_slot    = 0;

    /** Version string returned by the last successful version check. */
    char     _pending_version[HSYS_OTA_MAX_VERSION_LEN];

    /** Expected CRC32 from check response (0 = skip verification). */
    uint32_t _pending_crc32  = 0U;

    /** Cached ota_fs_driver_t from MsgOtaDriverResponse (used in download). */
    const ota_fs_driver_t *_dl_drv = nullptr;
    void                  *_dl_ctx = nullptr;

    // ── Cached config ──────────────────────────────────────────────────────
    char        _server_url[HSYS_OTA_MAX_URL_LEN]      = {};
    char        _device_id [HSYS_OTA_MAX_DEVICE_ID_LEN]= {};
    const char *_cert_pem  = nullptr;

    // ── Internal helpers ───────────────────────────────────────────────────

    /** Build an hsys_ota_cfg_t for the given target slot using cached config. */
    bool _build_cfg(uint8_t slot, hsys_ota_cfg_t *out) const;

    /** Send MsgHttpStartRequest to ModuleHttp and transition _http_phase. */
    void _send_http_start(pal_http_method_t method, uint32_t timeout_ms);

    /** Burst-send SetRootCa + Content-Type header + SetUrl(check) + Body + Send. */
    void _burst_check_post(uint8_t slot);

    /** Burst-send SetRootCa + SetStreamSink + SetUrl(download) + Send. */
    void _burst_download_get(uint8_t slot,
                              const ota_fs_driver_t *drv, void *ctx,
                              uint32_t expected_crc32);

    /** Handle MsgHttpStartResponse (common for both check and download). */
    void _on_http_start_response(const hsys_msg_t &msg);

    /** Handle MsgHttpResult for the version-check POST. */
    void _on_check_result(const hsys_msg_t &msg);

    /** Handle MsgHttpResult for the firmware download GET. */
    void _on_download_result(const hsys_msg_t &msg);

    /** Publish a MsgOtaProgress message. */
    void _publish_progress(uint32_t bytes, uint32_t total, uint8_t pct);
};

