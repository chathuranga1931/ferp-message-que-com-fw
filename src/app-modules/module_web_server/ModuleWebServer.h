/**
 * @file ModuleWebServer.h
 * @brief Common HTTP configuration / OTA web server module.
 *
 * Works on both ESP32 (via pal_esp_idf_http_server.cpp) and the macOS
 * simulator (via pal_mac_http_server.cpp).  Uses only the pal_http_server.h
 * interface and HSYS OS primitives — no platform-specific code.
 *
 * Endpoints:
 *   GET  /                             → SPIFFS index.html
 *   GET  /api/config                   → current config JSON
 *   GET  /getDeviceConfigurations      → current config JSON (legacy alias)
 *   POST /api/config                   → JSON patch; triggers hot-reload
 *   POST /setDeviceConfigurationsPost  → JSON patch (legacy alias)
 *   GET  /api/status                   → {"running":true,"port":8080}
 *   GET  /api/ota/status               → {"state":"idle|uploading","bytes":N}
 *   POST /updateFirmwareBin            → multipart firmware upload, target 0
 *   POST /updateDisplayTapBootloaderBin→ multipart upload, target 1
 *   POST /updateDisplayTapPartitionsBin→ multipart upload, target 2
 *   POST /updateDisplayTapBin          → multipart upload, target 3
 *
 * OTA upload protocol:
 *   The upload handler performs the HSYS OTA handshake:
 *     1. MsgOtaStartRequest  → OtaModule
 *     2. wait MsgOtaStartResponse (5 s, via m_start_resp_sem)
 *     3. MsgOtaRequestDriver → OtaModule
 *     4. wait MsgOtaDriverResponse (5 s, via m_driver_resp_sem)
 *     5. ota_fs_driver_t fopen/fwrite/fclose
 *     6. MsgOtaCompleteNotify → OtaModule
 *
 * Shell usage (works on both platforms):
 *   curl -F "file=@firmware.bin"   http://localhost:8080/updateFirmwareBin
 *   curl http://localhost:8080/api/config
 *   curl -X POST -H "Content-Type: application/json" \
 *        -d '{"ssid":"NewNet"}' http://localhost:8080/api/config
 */
#pragma once

#include "hsys_module.h"
#include "hsys_semaphore.h"
#include "hsys_mutex.h"
#include "pal_http_server.h"
#include "app_module_ids.h"
#include "FileSystemDriver.h"        // ota_fs_driver_t

#include "msg_ota_start_response.h"
#include "msg_ota_driver_response.h"

// ---------------------------------------------------------------------------

class ModuleWebServer : public HsysModule
{
public:
    static constexpr hsys_module_id_t MODULE_ID = MODULE_WEB_SERVER_ID;
    static constexpr uint16_t         HTTP_PORT  = 8080;

    ModuleWebServer();
    static ModuleWebServer *instance();

protected:
    void pre_init()  override;
    void init()      override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    /* ── HTTP server handle ────────────────────────────────────────────── */
    pal_http_server_handle_t m_server = nullptr;

    /* ── OTA handshake state ─────────────────────────────────────────── */
    hsys_semaphore_handle_t m_start_resp_sem  = nullptr; ///< signaled by on_msg_received
    hsys_semaphore_handle_t m_driver_resp_sem = nullptr; ///< signaled by on_msg_received
    hsys_mutex_handle_t     m_ota_lock        = nullptr;
    volatile bool           m_ota_busy        = false;
    ota_start_result_t      m_start_result    = OTA_START_REJECTED_BUSY;
    const ota_fs_driver_t  *m_ota_driver      = nullptr;
    void                   *m_ota_ctx         = nullptr;
    volatile uint32_t       m_ota_bytes       = 0;       ///< bytes written so far

    /* ── Per-endpoint OTA context (user_ctx for upload handlers) ─────── */
    struct OtaUpCtx {
        ModuleWebServer *self;
        uint8_t          target_idx;
    };
    OtaUpCtx m_ota_ep[4];   ///< indexed by target_idx (0-3)

    /* ── HTTP handler callbacks (static — take self via user_ctx) ─────── */
    static int32_t _hdl_get_root   (pal_http_request_t req, void *ctx);
    static int32_t _hdl_get_config (pal_http_request_t req, void *ctx);
    static int32_t _hdl_post_config(pal_http_request_t req, void *ctx);
    static int32_t _hdl_get_status (pal_http_request_t req, void *ctx);
    static int32_t _hdl_ota_status (pal_http_request_t req, void *ctx);

    /** Called per chunk (or once with full binary on mac PAL). */
    static int32_t _hdl_fw_upload(pal_http_request_t req,
                                   const char *filename,
                                   size_t offset,
                                   const uint8_t *data,
                                   size_t len,
                                   bool is_final,
                                   void *user_ctx);

    /* ── Helpers callable from static handlers (same class scope → access
     *    protected HsysModule::send/publish) ────────────────────────── */
    bool _ota_send_start_request(uint8_t target_idx, const char *ver);
    bool _ota_send_driver_request();
    bool _ota_publish_progress(uint8_t target_idx,
                               uint32_t written, uint32_t total);
    bool _ota_send_complete_notify(bool success);
    void _trigger_config_reload();
};
