/**
 * @file ModuleWebServer.cpp
 * @brief Common HTTP configuration / OTA web server HSYS module.
 *
 * Platform-independent implementation using pal_http_server.h for HTTP and
 * hsys_semaphore / hsys_mutex for cross-task synchronisation with OtaModule.
 *
 * The OTA upload handler blocks (with timeout) on semaphores until OtaModule
 * responds via the HSYS message bus.  The HTTP server task and the HSYS module
 * task are separate; on_msg_received() runs in the HSYS task and signals the
 * semaphores so the HTTP task can proceed.
 */

#include "ModuleWebServer.h"
#include "pal_logger.h"
#include "pal_spiffs.h"

/* HSYS messages */
#include "msg_spiffs_ready.h"
#include "msg_ota_start_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_request_driver.h"
#include "msg_ota_driver_response.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_progress.h"

#include "app_module_ids.h"

#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>

#define __TAG__        "WEBSRV__"
#ifndef WEB_SRV_LOG_EN
#define WEB_SRV_LOG_EN true
#endif

/** SPIFFS path for the device configuration file (platform-independent). */
static constexpr const char *k_config_path = "Configs/DeviceConfigs.json";

/* ── Singleton ──────────────────────────────────────────────────────────── */

static ModuleWebServer s_instance;
ModuleWebServer *ModuleWebServer::instance() { return &s_instance; }

/* ── Constructor ─────────────────────────────────────────────────────────── */

ModuleWebServer::ModuleWebServer()
    : HsysModule(MODULE_ID, "WEBSRV__")
{
    for (int i = 0; i < 4; i++) {
        m_ota_ep[i].self       = this;
        m_ota_ep[i].target_idx = (uint8_t)i;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

void ModuleWebServer::pre_init()
{
    /* ── Allocate synchronisation primitives ─────────────────────────────── */
    m_ota_lock        = hsys_mutex_create();
    m_start_resp_sem  = hsys_semaphore_create(false);  /* initially unavailable */
    m_driver_resp_sem = hsys_semaphore_create(false);

    /* ── Start HTTP server ───────────────────────────────────────────────── */
    pal_http_server_config_t cfg = {};
    cfg.port              = HTTP_PORT;
    cfg.max_uri_handlers  = 16;
    cfg.max_open_sockets  = 7;
    cfg.stack_size        = 8192;
    cfg.recv_wait_timeout = 30000;   /* 30 s — OTA handshake may block briefly */
    cfg.send_wait_timeout = 10000;

    if (pal_http_server_start(&cfg, &m_server) != PAL_OK) {
        LOG_MSG_ERROR(WEB_SRV_LOG_EN, "Failed to start HTTP server on port %d",
                      (int)HTTP_PORT);
        return;
    }

    /* ── Register URI handlers ───────────────────────────────────────────── */

    /* Static root */
    pal_http_server_register_uri(m_server, "/",
        PAL_HTTP_GET, _hdl_get_root, this);

    /* Config read / write */
    pal_http_server_register_uri(m_server, "/api/config",
        PAL_HTTP_GET,  _hdl_get_config, this);
    pal_http_server_register_uri(m_server, "/api/config",
        PAL_HTTP_POST, _hdl_post_config, this);
    /* Legacy aliases (compatible with old app URLs) */
    pal_http_server_register_uri(m_server, "/getDeviceConfigurations",
        PAL_HTTP_GET,  _hdl_get_config, this);
    pal_http_server_register_uri(m_server, "/setDeviceConfigurationsPost",
        PAL_HTTP_POST, _hdl_post_config, this);

    /* Status */
    pal_http_server_register_uri(m_server, "/api/status",
        PAL_HTTP_GET, _hdl_get_status, this);
    pal_http_server_register_uri(m_server, "/api/ota/status",
        PAL_HTTP_GET, _hdl_ota_status, this);

    /* Firmware upload endpoints (multipart POST) */
    pal_http_server_register_uri_with_upload(m_server, "/updateFirmwareBin",
        nullptr, _hdl_fw_upload, &m_ota_ep[0]);
    pal_http_server_register_uri_with_upload(m_server, "/updateDisplayTapBootloaderBin",
        nullptr, _hdl_fw_upload, &m_ota_ep[1]);
    pal_http_server_register_uri_with_upload(m_server, "/updateDisplayTapPartitionsBin",
        nullptr, _hdl_fw_upload, &m_ota_ep[2]);
    pal_http_server_register_uri_with_upload(m_server, "/updateDisplayTapBin",
        nullptr, _hdl_fw_upload, &m_ota_ep[3]);
}

void ModuleWebServer::init()
{
    subscribe(MsgOtaStartResponse::ID);
    subscribe(MsgOtaDriverResponse::ID);

    if (m_server) {
        LOG_MSG_INFO(WEB_SRV_LOG_EN,
                     "Web server ready: http://localhost:%d", (int)HTTP_PORT);
    }
}

void ModuleWebServer::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {

        case MsgOtaStartResponse::ID: {
            auto p = MsgOtaStartResponse::deserialize(msg);
            hsys_mutex_lock(m_ota_lock);
            m_start_result = p.result;
            hsys_mutex_unlock(m_ota_lock);
            hsys_semaphore_give(m_start_resp_sem);
            break;
        }

        case MsgOtaDriverResponse::ID: {
            auto p = MsgOtaDriverResponse::deserialize(msg);
            hsys_mutex_lock(m_ota_lock);
            m_ota_driver = p.driver;
            m_ota_ctx    = p.ctx;
            hsys_mutex_unlock(m_ota_lock);
            hsys_semaphore_give(m_driver_resp_sem);
            break;
        }

        default:
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * OTA message helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

bool ModuleWebServer::_ota_send_start_request(uint8_t target_idx,
                                               const char *ver)
{
    MsgOtaStartRequest::Payload p{};
    p.target_idx = target_idx;
    strncpy(p.incoming_version, ver, sizeof(p.incoming_version) - 1);
    auto *msg = MsgOtaStartRequest::create(MODULE_ID, p);
    if (!msg) return false;
    send(msg, MODULE_OTA_ID);
    return true;
}

bool ModuleWebServer::_ota_send_driver_request()
{
    auto *msg = MsgOtaRequestDriver::create(MODULE_ID);
    if (!msg) return false;
    send(msg, MODULE_OTA_ID);
    return true;
}

bool ModuleWebServer::_ota_publish_progress(uint8_t target_idx,
                                             uint32_t written, uint32_t total)
{
    MsgOtaProgress::Payload p{};
    p.target_idx    = target_idx;
    p.bytes_written = written;
    p.total_bytes   = total;
    p.percent       = (total > 0) ? (uint8_t)((written * 100u) / total) : 0;
    auto *msg = MsgOtaProgress::create(MODULE_ID, p);
    if (!msg) return false;
    publish(msg);
    return true;
}

bool ModuleWebServer::_ota_send_complete_notify(bool success)
{
    MsgOtaCompleteNotify::Payload p{};
    p.success    = success;
    p.last_error = success ? OTA_FS_OK : OTA_FS_ERR_WRITE_FAIL;
    auto *msg = MsgOtaCompleteNotify::create(MODULE_ID, p);
    if (!msg) return false;
    send(msg, MODULE_OTA_ID);
    return true;
}

void ModuleWebServer::_trigger_config_reload()
{
    auto *msg = MsgSpiffsReady::create(MODULE_ID);
    if (msg) publish(msg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Config handlers
 * ═══════════════════════════════════════════════════════════════════════════ */

int32_t ModuleWebServer::_hdl_get_root(pal_http_request_t req, void * /*ctx*/)
{
    pal_http_resp_set_type(req, "text/html; charset=utf-8");
    return pal_http_resp_send_file(req, "index.html", nullptr);
}

int32_t ModuleWebServer::_hdl_get_config(pal_http_request_t req, void * /*ctx*/)
{
    char buf[4096] = "{}";
    size_t bytes_read = 2;
    pal_spiffs_file_read(k_config_path, (uint8_t *)buf, sizeof(buf) - 1,
                         &bytes_read);
    buf[bytes_read] = '\0';

    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req, buf, bytes_read);
}

int32_t ModuleWebServer::_hdl_post_config(pal_http_request_t req, void *ctx)
{
    ModuleWebServer *self = (ModuleWebServer *)ctx;

    /* Read POST body */
    size_t content_len = pal_http_req_get_content_len(req);
    if (content_len == 0) {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"empty body\"}", 0);
    }

    static constexpr size_t k_max = 4096;
    char body[k_max + 1];
    size_t received = 0;

    /* Read in a loop until we have all content_len bytes */
    while (received < content_len && received < k_max) {
        size_t got = 0;
        if (pal_http_req_recv(req, body + received,
                              k_max - received, &got) != PAL_OK || got == 0) {
            break;
        }
        received += got;
    }
    body[received] = '\0';

    /* Parse incoming JSON */
    JsonDocument new_doc;
    if (deserializeJson(new_doc, body, received) != DeserializationError::Ok) {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"invalid JSON\"}", 0);
    }

    /* Read existing config */
    char existing[4096] = "{}";
    size_t existing_len = 2;
    pal_spiffs_file_read(k_config_path, (uint8_t *)existing,
                         sizeof(existing) - 1, &existing_len);
    existing[existing_len] = '\0';

    JsonDocument existing_doc;
    deserializeJson(existing_doc, existing, existing_len);

    /* Merge — new values overwrite matching keys */
    for (JsonPair kv : new_doc.as<JsonObject>()) {
        existing_doc[kv.key()] = kv.value();
    }

    /* Serialise and write back */
    char out[4096];
    size_t out_len = serializeJsonPretty(existing_doc, out, sizeof(out));
    pal_spiffs_file_write(k_config_path, (uint8_t *)out, out_len);

    /* Hot-reload */
    self->_trigger_config_reload();

    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req,
        "{\"ok\":true,\"hot_reload\":true}", 0);
}

int32_t ModuleWebServer::_hdl_get_status(pal_http_request_t req, void * /*ctx*/)
{
    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req,
        "{\"running\":true,\"port\":8080}", 0);
}

int32_t ModuleWebServer::_hdl_ota_status(pal_http_request_t req, void *ctx)
{
    ModuleWebServer *self = (ModuleWebServer *)ctx;

    hsys_mutex_lock(self->m_ota_lock);
    bool     busy  = self->m_ota_busy;
    uint32_t bytes = self->m_ota_bytes;
    hsys_mutex_unlock(self->m_ota_lock);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"state\":\"%s\",\"bytes\":%u}",
             busy ? "uploading" : "idle",
             (unsigned)bytes);

    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req, resp, 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Firmware upload handler
 *
 * Called by the PAL once per chunk (or once with full binary on mac PAL).
 * On the first chunk (offset == 0): performs the full HSYS OTA handshake.
 * On subsequent chunks: writes data.
 * On the last chunk (is_final == true): closes the OTA session.
 * ═══════════════════════════════════════════════════════════════════════════ */

int32_t ModuleWebServer::_hdl_fw_upload(pal_http_request_t req,
                                         const char *filename,
                                         size_t offset,
                                         const uint8_t *data,
                                         size_t len,
                                         bool is_final,
                                         void *user_ctx)
{
    OtaUpCtx       *upctx     = (OtaUpCtx *)user_ctx;
    ModuleWebServer *self      = upctx->self;
    uint8_t          target    = upctx->target_idx;

    /* ── First chunk: OTA handshake ───────────────────────────────────────── */
    if (offset == 0) {
        /* Guard concurrent uploads */
        hsys_mutex_lock(self->m_ota_lock);
        bool already_busy = self->m_ota_busy;
        if (!already_busy) {
            self->m_ota_busy   = true;
            self->m_ota_bytes  = 0;
            self->m_ota_driver = nullptr;
            self->m_ota_ctx    = nullptr;
        }
        hsys_mutex_unlock(self->m_ota_lock);

        if (already_busy) {
            LOG_MSG_WARNING(WEB_SRV_LOG_EN,
                            "OTA: upload rejected — session already active");
            pal_http_resp_set_status(req, 503);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"ota already in progress\"}", 0);
            return PAL_ERROR;
        }

        /* Send MsgOtaStartRequest → OtaModule */
        LOG_MSG_INFO(WEB_SRV_LOG_EN,
                     "OTA: requesting session (target=%u file='%s')",
                     (unsigned)target, filename);
        if (!self->_ota_send_start_request(target, "web-upload")) {
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 500);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"start-request alloc failed\"}", 0);
            return PAL_ERROR;
        }

        /* Wait for MsgOtaStartResponse (5 s) */
        if (!hsys_semaphore_take_timeout(self->m_start_resp_sem, 5000)) {
            LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA: start-response timed out");
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 503);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"ota start timed out\"}", 0);
            return PAL_ERROR;
        }

        hsys_mutex_lock(self->m_ota_lock);
        ota_start_result_t result = self->m_start_result;
        hsys_mutex_unlock(self->m_ota_lock);

        if (result != OTA_START_ACCEPTED) {
            LOG_MSG_WARNING(WEB_SRV_LOG_EN,
                            "OTA: start rejected (result=%d)", (int)result);
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 503);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"ota rejected\"}", 0);
            return PAL_ERROR;
        }

        /* Send MsgOtaRequestDriver → OtaModule */
        if (!self->_ota_send_driver_request()) {
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 500);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"driver-request alloc failed\"}", 0);
            return PAL_ERROR;
        }

        /* Wait for MsgOtaDriverResponse (5 s) */
        if (!hsys_semaphore_take_timeout(self->m_driver_resp_sem, 5000)) {
            LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA: driver-response timed out");
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 500);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"ota driver timed out\"}", 0);
            return PAL_ERROR;
        }

        hsys_mutex_lock(self->m_ota_lock);
        const ota_fs_driver_t *drv = self->m_ota_driver;
        void                  *dctx = self->m_ota_ctx;
        hsys_mutex_unlock(self->m_ota_lock);

        if (!drv) {
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 500);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"null ota driver\"}", 0);
            return PAL_ERROR;
        }

        /* Open the OTA target */
        ota_fs_err_t err = drv->fopen(dctx, filename, OTA_FS_OPEN_WRITE);
        if (err != OTA_FS_OK) {
            LOG_MSG_ERROR(WEB_SRV_LOG_EN,
                          "OTA: driver fopen failed (err=%d)", (int)err);
            self->_ota_send_complete_notify(false);
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 500);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"driver fopen failed\"}", 0);
            return PAL_ERROR;
        }

        LOG_MSG_INFO(WEB_SRV_LOG_EN,
                     "OTA: session open — streaming binary (target=%u)",
                     (unsigned)target);
    }

    /* ── Write this chunk ──────────────────────────────────────────────────── */
    if (len > 0) {
        hsys_mutex_lock(self->m_ota_lock);
        const ota_fs_driver_t *drv = self->m_ota_driver;
        void                  *dctx = self->m_ota_ctx;
        hsys_mutex_unlock(self->m_ota_lock);

        ota_fs_err_t err = drv->fwrite(dctx, data, (uint32_t)len);
        if (err != OTA_FS_OK) {
            LOG_MSG_ERROR(WEB_SRV_LOG_EN,
                          "OTA: fwrite failed at offset %zu (err=%d)",
                          offset, (int)err);
            drv->ferase(dctx);
            self->_ota_send_complete_notify(false);
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 500);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"write failed\"}", 0);
            return PAL_ERROR;
        }

        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_bytes += (uint32_t)len;
        uint32_t total_written = self->m_ota_bytes;
        hsys_mutex_unlock(self->m_ota_lock);

        self->_ota_publish_progress(target, total_written, 0);
    }

    /* ── Final chunk: close and notify ────────────────────────────────────── */
    if (is_final) {
        hsys_mutex_lock(self->m_ota_lock);
        const ota_fs_driver_t *drv = self->m_ota_driver;
        void                  *dctx = self->m_ota_ctx;
        uint32_t total_bytes = self->m_ota_bytes;
        hsys_mutex_unlock(self->m_ota_lock);

        bool ok = (drv->fclose(dctx) == OTA_FS_OK);
        if (!ok) {
            drv->ferase(dctx);
            LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA: fclose failed");
        }

        self->_ota_send_complete_notify(ok);

        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy  = false;
        self->m_ota_bytes = 0;
        hsys_mutex_unlock(self->m_ota_lock);

        if (ok) {
            LOG_MSG_INFO(WEB_SRV_LOG_EN,
                         "OTA: complete — %u B written", (unsigned)total_bytes);
            char resp[128];
            snprintf(resp, sizeof(resp),
                     "{\"ok\":true,\"bytes\":%u}", (unsigned)total_bytes);
            pal_http_resp_set_type(req, "application/json");
            pal_http_resp_send(req, resp, 0);
        } else {
            pal_http_resp_set_status(req, 500);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"write failed\"}", 0);
        }

        return ok ? PAL_OK : PAL_ERROR;
    }

    return PAL_OK;
}
