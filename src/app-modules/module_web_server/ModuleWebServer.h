/**
 * @file ModuleWebServer.h
 * @brief Common HTTP configuration / OTA web server module.
 *
 * Works on both ESP32 (via pal_esp_idf_http_server.cpp) and the macOS
 * simulator (via pal_mac_http_server.cpp).  Uses only the pal_http_server.h
 * interface and HSYS OS primitives — no platform-specific code.
 *
 * Endpoints:
 *   GET  /api/config                   → current config JSON
 *   GET  /getDeviceConfigurations      → current config JSON (legacy alias)
 *   POST /api/config                   → JSON patch; triggers hot-reload
 *   POST /setDeviceConfigurationsPost  → JSON patch (legacy alias)
 *   GET  /api/status                   → {"running":true,"port":8080}
 *   GET  /api/ota/status               → {"state":"idle|uploading","bytes":N}
 *   POST /api/ota/bin?name=<bin>       → multipart firmware upload (target from table)
 *   POST /api/ota/start?name=<bin>     → chunked OTA: open session
 *   POST /api/ota/chunk?seq=<N>        → chunked OTA: write one chunk (raw binary body)
 *   POST /api/ota/complete             → chunked OTA: commit and close
 *   POST /api/messages                 → HTTP→message-bus bridge
 *   GET  /...                             → static file from SPIFFS (wildcard, table-driven)
 *
 * OTA upload protocol (both /api/ota/bin and /api/ota/start+chunk+complete):
 *   The upload handler performs the HSYS OTA handshake:
 *     1. MsgOtaStartRequest  → OtaModule
 *     2. wait MsgOtaStartResponse (5 s, via m_start_resp_sem)
 *     3. MsgOtaRequestDriver → OtaModule
 *     4. wait MsgOtaDriverResponse (5 s, via m_driver_resp_sem)
 *     5. ota_fs_driver_t fopen/fwrite/fclose
 *     6. MsgOtaCompleteNotify → OtaModule
 *
 * Chunked OTA mirrors the MQTT OTA session protocol:
 *   POST /api/ota/start  → handshake + fopen; returns {"ok":true,"chunk_size":4096}
 *   POST /api/ota/chunk  → fwrite one chunk; seq enforced in order
 *   POST /api/ota/complete → fclose + MsgOtaCompleteNotify
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
#include "app_msg_codec.h"           // APP_MSG_CODEC_DATA_JSON_MAX, codec API

#include "msg_ota_start_response.h"
#include "msg_ota_driver_response.h"

// ---------------------------------------------------------------------------

class ModuleWebServer : public HsysModule
{
public:
    static constexpr hsys_module_id_t MODULE_ID      = MODULE_WEB_SERVER_ID;
    static constexpr uint16_t         HTTP_PORT       = 8080;
    static constexpr uint32_t         OTA_CHUNK_MAX   = 4096;  ///< max body size for /api/ota/chunk

    ModuleWebServer();
    static ModuleWebServer *instance();

    /** One row maps an HTTP URI to a filename served via the supplied file driver.
     *  Terminate the table with {nullptr, nullptr, nullptr}. */
    struct StaticFileDef {
        const char                   *uri;      ///< HTTP path, e.g. "/" or "/styles.css"
        const char                   *filename; ///< filename (no leading slash), e.g. "styles.css"
        const pal_http_file_driver_t *driver;   ///< thread-safe read driver (app_spiffs / app_sd)
    };

    /** One row maps an OTA binary name (from ?name= query param) to a target index.
     *  Terminate the table with {nullptr, 0}. */
    struct OtaTargetDef {
        const char *name;        ///< Value of the ?name= query parameter, e.g. "main"
        uint8_t     target_idx;  ///< OtaModule target index (0-3)
    };

    /** One row maps an inbound message ID to a destination and optional
     *  expected response ID for the HTTP-to-message-bus bridge.
     *  Terminate the table with {0, 0, 0}. */
    struct ApiMsgRouteDef {
        hsys_msg_id_t    msg_id;      ///< Request message to decode and send/publish
        hsys_module_id_t dest_module; ///< Direct destination; 0 = broadcast (publish)
        hsys_msg_id_t    response_id; ///< Expected reply ID; 0 = fire-and-forget
    };

    /** Supply the URI->filename table used by the wildcard GET handler. */
    void set_static_files(const StaticFileDef  *table);
    /** Supply the binary-name->target-index table used by POST /api/ota/bin. */
    void set_ota_targets (const OtaTargetDef   *table);
    /** Supply the HTTP-to-message-bus route table used by POST /api/messages. */
    void set_api_routes  (const ApiMsgRouteDef *table);

protected:
    void pre_init()  override;
    void init()      override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    /* ── HTTP server handle ────────────────────────────────────────────── */
    pal_http_server_handle_t m_server = nullptr;

    /* ── Routing tables (set before framework init) ───────────────────── */
    const StaticFileDef   *m_static_files     = nullptr;
    const OtaTargetDef    *m_ota_targets      = nullptr;
    const ApiMsgRouteDef  *m_api_routes       = nullptr;

    /* ── OTA handshake state ─────────────────────────────────────────── */
    hsys_semaphore_handle_t m_start_resp_sem  = nullptr;
    hsys_semaphore_handle_t m_driver_resp_sem = nullptr;
    hsys_mutex_handle_t     m_ota_lock        = nullptr;
    volatile bool           m_ota_busy        = false;
    volatile uint8_t        m_active_ota_target = 0;
    ota_start_result_t      m_start_result    = OTA_START_REJECTED_BUSY;
    const ota_fs_driver_t  *m_ota_driver      = nullptr;
    void                   *m_ota_ctx         = nullptr;
    volatile uint32_t       m_ota_bytes        = 0;
    volatile uint32_t       m_ota_total_bytes  = 0;  ///< total firmware size (from Content-Length or start body)
    volatile uint32_t       m_ota_expected_seq = 0;  ///< next expected chunk seq (chunked-upload mode)

    /* ── API message-bus bridge state ────────────────────────────────── */
    hsys_mutex_handle_t     m_api_lock        = nullptr;
    hsys_semaphore_handle_t m_api_resp_sem    = nullptr;
    volatile hsys_msg_id_t  m_api_wait_id     = 0;
    char m_api_resp_data[APP_MSG_CODEC_DATA_JSON_MAX + 1];
    char m_api_msg_name [APP_MSG_CODEC_MSG_NAME_MAX  + 1];

    /* ── HTTP handler callbacks (static — take self via user_ctx) ─────── */
    static int32_t _hdl_static_file  (pal_http_request_t req, void *ctx);
    static int32_t _hdl_get_config   (pal_http_request_t req, void *ctx);
    static int32_t _hdl_post_config  (pal_http_request_t req, void *ctx);
    static int32_t _hdl_get_status   (pal_http_request_t req, void *ctx);
    static int32_t _hdl_ota_status   (pal_http_request_t req, void *ctx);
    static int32_t _hdl_post_message (pal_http_request_t req, void *ctx);
    /* Chunked OTA endpoints (mirrors MQTT session protocol) */
    static int32_t _hdl_ota_start    (pal_http_request_t req, void *ctx);
    static int32_t _hdl_ota_chunk    (pal_http_request_t req, void *ctx);
    static int32_t _hdl_ota_complete (pal_http_request_t req, void *ctx);

    /** Called per chunk (or once with full binary on mac PAL). */
    static int32_t _hdl_fw_upload(pal_http_request_t req,
                                   const char *filename,
                                   size_t offset,
                                   const uint8_t *data,
                                   size_t len,
                                   bool is_final,
                                   void *user_ctx);

    /* ── OTA helpers ────────────────────────────────────────────────────── */
    bool _ota_send_start_request(uint8_t target_idx, const char *ver);
    bool _ota_send_driver_request();
    bool _ota_publish_progress(uint8_t target_idx, uint32_t written, uint32_t total);
    bool _ota_send_complete_notify(bool success);
    void _trigger_config_reload();
};
