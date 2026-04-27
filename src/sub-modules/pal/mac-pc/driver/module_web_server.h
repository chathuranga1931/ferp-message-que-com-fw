/**
 * @file module_web_server.h
 * @brief Simulator-only HTTP configuration server (port 8080).
 *
 * Listens on localhost:8080 and exposes device configuration for browser
 * and shell-script (curl) access.  Started automatically as an extra HSYS
 * module registered in pal_mac_system.cpp.
 *
 * Endpoints:
 *   GET  /                     → HTML configuration UI
 *   GET  /api/config           → current config as JSON
 *   POST /api/config           → merge JSON patch into config file;
 *                                triggers ModuleConfig hot-reload via MsgSpiffsReady
 *   GET  /api/status           → {"running":true,"port":8080}
 *   GET  /api/ota/status                → {"state":"idle|uploading","bytes":N}
 *   POST /updateFirmwareBin             → ESP32 main firmware (target_idx=0)
 *   POST /updateDisplayTapBootloaderBin → dispTap bootloader.bin (target_idx=1)
 *   POST /updateDisplayTapPartitionsBin → dispTap partitions.bin (target_idx=2)
 *   POST /updateDisplayTapBin           → dispTap firmware.bin   (target_idx=3)
 *
 * Shell usage:
 *   curl -F "file=@firmware.bin"      http://localhost:8080/updateFirmwareBin
 *   curl -F "file=@bootloader.bin"    http://localhost:8080/updateDisplayTapBootloaderBin
 *   curl -F "file=@partitions.bin"    http://localhost:8080/updateDisplayTapPartitionsBin
 *   curl -F "file=@dispTap.bin"       http://localhost:8080/updateDisplayTapBin
 *
 *   # Poll OTA status
 *   curl http://localhost:8080/api/ota/status
 *
 *   # Update one or more fields (partial patch)
 *   curl -X POST http://localhost:8080/api/config \
 *        -H "Content-Type: application/json" \
 *        -d '{"ssid":"HomeNet","password":"s3cr3t"}'
 *
 *   # Replace entire config from a file
 *   curl -X POST http://localhost:8080/api/config \
 *        -H "Content-Type: application/json" \
 *        -d @config.json
 *
 * Config file location (relative to simulator cwd):
 *   SPIFFS/spiffs/Configs/DeviceConfigs.json
 */
#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"

// ---------------------------------------------------------------------------
// ModuleWebServer
// ---------------------------------------------------------------------------

class ModuleWebServer : public HsysModule
{
public:
    static constexpr hsys_module_id_t MODULE_ID = MODULE_WEB_SERVER_ID;
    static constexpr uint16_t         HTTP_PORT  = 8080;

    ModuleWebServer() : HsysModule(MODULE_ID, "web_server") {}

    static ModuleWebServer *instance();

    /** Publish MsgSpiffsReady to trigger ModuleConfig hot-reload.
     *  Wraps the protected publish() so it can be called from the
     *  HTTP listener pthread (a non-member static context). */
    static bool publish_reload();

protected:
    void pre_init() override;               ///< Opens socket + starts listener thread
    void init()     override;               ///< Logs the server URL
    void on_msg_received(const hsys_msg_t &msg) override;   ///< No-op

private:
    /** Background pthread: accept loop */
    static void *_listener_thread(void *arg);

    /** Handle one HTTP connection (called from _listener_thread) */
    static void  _handle_connection(int client_fd);

    // ── OTA session helpers ───────────────────────────────────────────────────
    // Each wraps a protected send() / publish() call so it can be invoked from
    // the static HTTP listener pthread context (same pattern as publish_reload).
    static bool send_ota_start_request(uint8_t target_idx, const char *version);
    static bool send_ota_request_driver();
    static bool publish_ota_progress(uint8_t target_idx,
                                     uint32_t bytes_written, uint32_t total_bytes);
    static bool send_ota_complete_notify(bool success);

    // ── Additional HTTP handlers ──────────────────────────────────────────────
    /** Handle POST /updateFirmwareBin or /updateDisplayTapBin — streams
     *  multipart binary through OtaModule using the given target_idx. */
    static void _handle_post_firmware(int fd, int content_length,
                                      const char *content_type_hdr,
                                      uint8_t target_idx);

    /** Handle GET /api/ota/status */
    static void _handle_get_ota_status(int fd);
};
