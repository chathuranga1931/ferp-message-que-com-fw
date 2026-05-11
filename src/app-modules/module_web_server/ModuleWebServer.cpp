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
#include "pal_http_server.h"

/* HSYS messages */
#include "msg_spiffs_ready.h"
#include "msg_ota_start_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_request_driver.h"
#include "msg_ota_driver_response.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_progress.h"

#include "app_module_ids.h"
#include "app_msg_codec.h"
#include "hsys_msg.h"

#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
    memset(m_api_resp_data, 0, sizeof(m_api_resp_data));
    memset(m_api_msg_name,  0, sizeof(m_api_msg_name));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

void ModuleWebServer::pre_init()
{
    /* ── Allocate synchronisation primitives ─────────────────────────────── */
    m_ota_lock        = hsys_mutex_create();
    m_api_lock        = hsys_mutex_create();
    m_start_resp_sem  = hsys_semaphore_create(false);
    m_driver_resp_sem = hsys_semaphore_create(false);
    m_api_resp_sem    = hsys_semaphore_create(false);

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

    /* ── Register URI handlers (specific routes first, wildcard last) ─────── */

    /* Config read / write */
    pal_http_server_register_uri(m_server, "/api/config",
        PAL_HTTP_GET,  _hdl_get_config, this);
    pal_http_server_register_uri(m_server, "/api/config",
        PAL_HTTP_POST, _hdl_post_config, this);
    /* Legacy aliases */
    pal_http_server_register_uri(m_server, "/getDeviceConfigurations",
        PAL_HTTP_GET,  _hdl_get_config, this);
    pal_http_server_register_uri(m_server, "/setDeviceConfigurationsPost",
        PAL_HTTP_POST, _hdl_post_config, this);

    /* Status */
    pal_http_server_register_uri(m_server, "/api/status",
        PAL_HTTP_GET, _hdl_get_status, this);
    pal_http_server_register_uri(m_server, "/api/ota/status",
        PAL_HTTP_GET, _hdl_ota_status, this);

    /* Chunked OTA endpoints — mirror MQTT session protocol (start/chunk/complete) */
    pal_http_server_register_uri(m_server, "/api/ota/start",
        PAL_HTTP_POST, _hdl_ota_start, this);
    pal_http_server_register_uri(m_server, "/api/ota/chunk",
        PAL_HTTP_POST, _hdl_ota_chunk, this);
    pal_http_server_register_uri(m_server, "/api/ota/complete",
        PAL_HTTP_POST, _hdl_ota_complete, this);

    /* Generic OTA firmware upload: POST /api/ota/bin?name=<binary-name> */
    pal_http_server_register_uri_with_upload(m_server, "/api/ota/bin",
        nullptr, _hdl_fw_upload, this);

    /* HTTP -> message-bus bridge */
    pal_http_server_register_uri(m_server, "/api/messages",
        PAL_HTTP_POST, _hdl_post_message, this);

    /* Wildcard catch-all for static files — MUST be registered last */
    pal_http_server_register_uri(m_server, "/*",
        PAL_HTTP_GET, _hdl_static_file, this);
}

void ModuleWebServer::init()
{
    subscribe(MsgOtaStartResponse::ID);
    subscribe(MsgOtaDriverResponse::ID);

    /* Subscribe to API bridge response IDs from the route table */
    if (m_api_routes) {
        for (const ApiMsgRouteDef *r = m_api_routes; r->msg_id != 0; r++) {
            if (r->response_id != 0) {
                subscribe(r->response_id);
            }
        }
    }

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

        default: {
            /* API bridge: capture the expected response message */
            hsys_msg_id_t wait_id = m_api_wait_id;
            if (wait_id != 0 && msg.msg_id == wait_id) {
                app_msg_codec_encode(&msg,
                                     m_api_msg_name,  sizeof(m_api_msg_name),
                                     m_api_resp_data, sizeof(m_api_resp_data));
                hsys_semaphore_give(m_api_resp_sem);
            }
            break;
        }
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

int32_t ModuleWebServer::_hdl_static_file(pal_http_request_t req, void *ctx)
{
    ModuleWebServer *self = (ModuleWebServer *)ctx;
    char uri[256];
    if (pal_http_req_get_uri(req, uri, sizeof(uri)) != PAL_OK || uri[0] == '\0') {
        pal_http_resp_set_status(req, 404);
        return pal_http_resp_send(req, "Not found", 0);
    }

    if (self && self->m_static_files) {
        for (const StaticFileDef *e = self->m_static_files; e->uri; e++) {
            if (strcmp(e->uri, uri) == 0) {
                return pal_http_resp_send_file(req, e->filename, nullptr, e->driver);
            }
        }
    }

    pal_http_resp_set_status(req, 404);
    return pal_http_resp_send(req, "Not found", 0);
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
    ModuleWebServer *self      = (ModuleWebServer *)user_ctx;

    /* ── First chunk: OTA handshake ───────────────────────────────────────── */
    if (offset == 0) {
        /* Guard concurrent uploads */
        hsys_mutex_lock(self->m_ota_lock);
        bool already_busy = self->m_ota_busy;
        if (!already_busy) {
            self->m_ota_busy        = true;
            self->m_ota_bytes       = 0;
            self->m_ota_total_bytes = (uint32_t)pal_http_req_get_content_len(req);
            self->m_ota_driver      = nullptr;
            self->m_ota_ctx         = nullptr;
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

        /* Resolve OTA target by ?name= query parameter */
        char name_buf[64] = {};
        pal_http_req_get_query_param(req, "name", name_buf, sizeof(name_buf));
        uint8_t target = 255;
        if (self->m_ota_targets) {
            for (const OtaTargetDef *t = self->m_ota_targets; t->name; t++) {
                if (strcmp(t->name, name_buf) == 0) {
                    target = t->target_idx;
                    break;
                }
            }
        }
        if (target == 255) {
            LOG_MSG_WARNING(WEB_SRV_LOG_EN,
                            "OTA: unknown target name '%s'", name_buf);
            hsys_mutex_lock(self->m_ota_lock);
            self->m_ota_busy = false;
            hsys_mutex_unlock(self->m_ota_lock);
            pal_http_resp_set_status(req, 400);
            pal_http_resp_send(req,
                "{\"ok\":false,\"error\":\"unknown ota target name\"}", 0);
            return PAL_ERROR;
        }
        hsys_mutex_lock(self->m_ota_lock);
        self->m_active_ota_target = target;
        hsys_mutex_unlock(self->m_ota_lock);

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
        uint32_t total_size    = self->m_ota_total_bytes;
        hsys_mutex_unlock(self->m_ota_lock);

        self->_ota_publish_progress(self->m_active_ota_target, total_written, total_size);
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Chunked OTA handlers  (/api/ota/start  /api/ota/chunk  /api/ota/complete)
 *
 * These three endpoints together mirror the MQTT OTA session protocol:
 *   1. POST /api/ota/start?name=<target>  Body: {"size":N,"crc32":N}
 *      Performs the full HSYS handshake (same as /api/ota/bin offset==0).
 *      Opens the ota_fs_driver_t write session.
 *      Response: {"ok":true,"chunk_size":4096}
 *
 *   2. POST /api/ota/chunk?seq=<N>  Body: raw binary (octet-stream)
 *      Writes one chunk.  seq must equal m_ota_expected_seq; 409 on mismatch
 *      so the tool knows exactly which seq to re-send on retry.
 *      Publishes MsgOtaProgress each call (resets OtaModule watchdog).
 *      Response: {"ok":true,"seq":N,"written":total_written}
 *
 *   3. POST /api/ota/complete  Body: {"crc32":N}  (crc32 informational)
 *      Calls fclose → MsgOtaCompleteNotify → OtaModule validates image.
 *      Response: {"ok":true,"bytes":total}
 * ═══════════════════════════════════════════════════════════════════════════ */

int32_t ModuleWebServer::_hdl_ota_start(pal_http_request_t req, void *ctx)
{
    ModuleWebServer *self = (ModuleWebServer *)ctx;

    /* ── Guard concurrent sessions ───────────────────────────────────────────────── */
    hsys_mutex_lock(self->m_ota_lock);
    bool already_busy = self->m_ota_busy;
    if (!already_busy) {
        self->m_ota_busy         = true;
        self->m_ota_bytes        = 0;
        self->m_ota_total_bytes  = 0;
        self->m_ota_expected_seq = 0;
        self->m_ota_driver       = nullptr;
        self->m_ota_ctx          = nullptr;
    }
    hsys_mutex_unlock(self->m_ota_lock);

    if (already_busy) {
        LOG_MSG_WARNING(WEB_SRV_LOG_EN, "OTA/chunk: rejected — session already active");
        pal_http_resp_set_status(req, 503);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"ota already in progress\"}", 0);
    }

    /* ── Resolve target from ?name= ─────────────────────────────────────────────── */
    char name_buf[64] = {};
    pal_http_req_get_query_param(req, "name", name_buf, sizeof(name_buf));
    uint8_t target = 255;
    if (self->m_ota_targets) {
        for (const OtaTargetDef *t = self->m_ota_targets; t->name; t++) {
            if (strcmp(t->name, name_buf) == 0) {
                target = t->target_idx;
                break;
            }
        }
    }
    if (target == 255) {
        LOG_MSG_WARNING(WEB_SRV_LOG_EN,
                        "OTA/chunk: unknown target '%s'", name_buf);
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy = false;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"unknown ota target name\"}", 0);
    }

    /* ── Parse body JSON: {"size": N, "crc32": N} ────────────────────────────────────── */
    size_t   content_len = pal_http_req_get_content_len(req);
    uint32_t total_size  = 0;
    if (content_len > 0 && content_len < 256) {
        char body[256] = {};
        size_t got = 0;
        pal_http_req_recv(req, body, content_len, &got);
        body[got] = '\0';
        JsonDocument doc;
        if (deserializeJson(doc, body, got) == DeserializationError::Ok) {
            total_size = doc["size"] | 0u;
        }
    }

    hsys_mutex_lock(self->m_ota_lock);
    self->m_active_ota_target = target;
    self->m_ota_total_bytes   = total_size;
    hsys_mutex_unlock(self->m_ota_lock);

    /* ── HSYS OTA handshake ───────────────────────────────────────────────────────────── */
    LOG_MSG_INFO(WEB_SRV_LOG_EN,
                 "OTA/chunk: requesting session (target=%u, size=%u)",
                 (unsigned)target, (unsigned)total_size);

    if (!self->_ota_send_start_request(target, "web-chunk")) {
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy = false;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"start-request alloc failed\"}", 0);
    }
    if (!hsys_semaphore_take_timeout(self->m_start_resp_sem, 5000)) {
        LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA/chunk: start-response timed out");
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy = false;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 503);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"ota start timed out\"}", 0);
    }

    hsys_mutex_lock(self->m_ota_lock);
    ota_start_result_t result = self->m_start_result;
    hsys_mutex_unlock(self->m_ota_lock);

    if (result != OTA_START_ACCEPTED) {
        LOG_MSG_WARNING(WEB_SRV_LOG_EN,
                        "OTA/chunk: start rejected (result=%d)", (int)result);
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy = false;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 503);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"ota rejected\"}", 0);
    }

    if (!self->_ota_send_driver_request()) {
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy = false;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"driver-request alloc failed\"}", 0);
    }
    if (!hsys_semaphore_take_timeout(self->m_driver_resp_sem, 5000)) {
        LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA/chunk: driver-response timed out");
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy = false;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"ota driver timed out\"}", 0);
    }

    hsys_mutex_lock(self->m_ota_lock);
    const ota_fs_driver_t *drv  = self->m_ota_driver;
    void                  *dctx = self->m_ota_ctx;
    hsys_mutex_unlock(self->m_ota_lock);

    if (!drv) {
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy = false;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"null ota driver\"}", 0);
    }

    /* ── Open OTA write session ───────────────────────────────────────────────────────── */
    ota_fs_err_t err = drv->fopen(dctx, nullptr, OTA_FS_OPEN_WRITE);
    if (err != OTA_FS_OK) {
        LOG_MSG_ERROR(WEB_SRV_LOG_EN,
                      "OTA/chunk: fopen failed (err=%d)", (int)err);
        self->_ota_send_complete_notify(false);
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy = false;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"driver fopen failed\"}", 0);
    }

    LOG_MSG_INFO(WEB_SRV_LOG_EN,
                 "OTA/chunk: session open (target=%u, size=%u)",
                 (unsigned)target, (unsigned)total_size);

    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req,
        "{\"ok\":true,\"chunk_size\":4096}", 0);
}

int32_t ModuleWebServer::_hdl_ota_chunk(pal_http_request_t req, void *ctx)
{
    ModuleWebServer *self = (ModuleWebServer *)ctx;

    /* Verify active session */
    hsys_mutex_lock(self->m_ota_lock);
    bool busy = self->m_ota_busy;
    hsys_mutex_unlock(self->m_ota_lock);
    if (!busy) {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"no active ota session\"}", 0);
    }

    /* Read ?seq= query parameter */
    char seq_buf[16] = {};
    pal_http_req_get_query_param(req, "seq", seq_buf, sizeof(seq_buf));
    uint32_t seq = (uint32_t)strtoul(seq_buf, nullptr, 10);

    /* Enforce in-order delivery */
    hsys_mutex_lock(self->m_ota_lock);
    uint32_t expected = self->m_ota_expected_seq;
    hsys_mutex_unlock(self->m_ota_lock);

    if (seq != expected) {
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"ok\":false,\"error\":\"seq mismatch\",\"expected\":%u}",
                 (unsigned)expected);
        pal_http_resp_set_status(req, 409);
        return pal_http_resp_send(req, resp, 0);
    }

    /* Validate chunk size */
    size_t content_len = pal_http_req_get_content_len(req);
    if (content_len == 0 || content_len > OTA_CHUNK_MAX) {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"invalid chunk size\"}", 0);
    }

    /* Heap-allocate receive buffer (avoids deep stack usage on ESP32) */
    uint8_t *chunk_buf = (uint8_t *)malloc(content_len);
    if (!chunk_buf) {
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"out of memory\"}", 0);
    }

    size_t received = 0;
    while (received < content_len) {
        size_t got = 0;
        if (pal_http_req_recv(req, (char *)chunk_buf + received,
                              content_len - received, &got) != PAL_OK || got == 0) {
            break;
        }
        received += got;
    }
    if (received != content_len) {
        free(chunk_buf);
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"incomplete chunk\"}", 0);
    }

    /* Write to OTA driver */
    hsys_mutex_lock(self->m_ota_lock);
    const ota_fs_driver_t *drv  = self->m_ota_driver;
    void                  *dctx = self->m_ota_ctx;
    hsys_mutex_unlock(self->m_ota_lock);

    ota_fs_err_t err = drv->fwrite(dctx, chunk_buf, (uint32_t)received);
    free(chunk_buf);

    if (err != OTA_FS_OK) {
        LOG_MSG_ERROR(WEB_SRV_LOG_EN,
                      "OTA/chunk: fwrite failed at seq %u (err=%d)",
                      (unsigned)seq, (int)err);
        drv->ferase(dctx);
        self->_ota_send_complete_notify(false);
        hsys_mutex_lock(self->m_ota_lock);
        self->m_ota_busy         = false;
        self->m_ota_expected_seq = 0;
        hsys_mutex_unlock(self->m_ota_lock);
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"write failed\"}", 0);
    }

    hsys_mutex_lock(self->m_ota_lock);
    self->m_ota_bytes        += (uint32_t)received;
    self->m_ota_expected_seq  = seq + 1;
    uint32_t total_written    = self->m_ota_bytes;
    uint32_t total_size       = self->m_ota_total_bytes;
    uint8_t  tgt              = self->m_active_ota_target;
    hsys_mutex_unlock(self->m_ota_lock);

    /* Publish MsgOtaProgress — resets OtaModule inactivity watchdog */
    self->_ota_publish_progress(tgt, total_written, total_size);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"seq\":%u,\"written\":%u}",
             (unsigned)seq, (unsigned)total_written);
    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req, resp, 0);
}

int32_t ModuleWebServer::_hdl_ota_complete(pal_http_request_t req, void *ctx)
{
    ModuleWebServer *self = (ModuleWebServer *)ctx;

    hsys_mutex_lock(self->m_ota_lock);
    bool                   busy  = self->m_ota_busy;
    const ota_fs_driver_t *drv   = self->m_ota_driver;
    void                  *dctx  = self->m_ota_ctx;
    uint32_t               total = self->m_ota_bytes;
    hsys_mutex_unlock(self->m_ota_lock);

    if (!busy || !drv) {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"no active ota session\"}", 0);
    }

    /* Log CRC32 from tool body (informational; ESP-IDF validates image on fclose) */
    size_t content_len = pal_http_req_get_content_len(req);
    if (content_len > 0 && content_len < 128) {
        char body[128] = {};
        size_t got = 0;
        pal_http_req_recv(req, body, content_len, &got);
        body[got] = '\0';
        JsonDocument doc;
        if (deserializeJson(doc, body, got) == DeserializationError::Ok) {
            uint32_t crc32 = doc["crc32"] | 0u;
            LOG_MSG_INFO(WEB_SRV_LOG_EN,
                         "OTA/chunk: complete  bytes=%u  tool_crc32=0x%08X",
                         (unsigned)total, (unsigned)crc32);
        }
    }

    bool ok = (drv->fclose(dctx) == OTA_FS_OK);
    if (!ok) {
        drv->ferase(dctx);
        LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA/chunk: fclose failed");
    }

    self->_ota_send_complete_notify(ok);

    hsys_mutex_lock(self->m_ota_lock);
    self->m_ota_busy         = false;
    self->m_ota_bytes        = 0;
    self->m_ota_expected_seq = 0;
    hsys_mutex_unlock(self->m_ota_lock);

    if (ok) {
        LOG_MSG_INFO(WEB_SRV_LOG_EN,
                     "OTA/chunk: committed — %u B written", (unsigned)total);
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"ok\":true,\"bytes\":%u}", (unsigned)total);
        pal_http_resp_set_type(req, "application/json");
        return pal_http_resp_send(req, resp, 0);
    } else {
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"commit failed\"}", 0);
    }
}
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Table setters
 * ═══════════════════════════════════════════════════════════════════════════ */

void ModuleWebServer::set_static_files(const StaticFileDef *table)
{
    m_static_files = table;
}

void ModuleWebServer::set_ota_targets(const OtaTargetDef *table)
{
    m_ota_targets = table;
}

void ModuleWebServer::set_api_routes(const ApiMsgRouteDef *table)
{
    m_api_routes = table;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HTTP -> message-bus bridge
 *
 * POST /api/messages
 * Body:     {"msg":"MsgXxx","data":{...}}
 * Response: {"ok":true,"msg":"MsgXxxResponse","data":{...}}
 *       or  {"ok":false,"error":"..."}
 *
 * Only messages whose msg_id appears in m_api_routes are accepted.
 * m_api_lock serialises concurrent HTTP requests.
 * m_api_resp_sem blocks until on_msg_received() delivers the response.
 * ═══════════════════════════════════════════════════════════════════════════ */

int32_t ModuleWebServer::_hdl_post_message(pal_http_request_t req, void *ctx)
{
    ModuleWebServer *self = (ModuleWebServer *)ctx;

    /* ── Read body ──────────────────────────────────────────────────────────────────── */
    size_t content_len = pal_http_req_get_content_len(req);
    if (content_len == 0) {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req, "{\"ok\":false,\"error\":\"empty body\"}", 0);
    }

    static constexpr size_t k_max_body = 768;
    char body[k_max_body + 1];
    size_t received = 0;
    while (received < content_len && received < k_max_body) {
        size_t got = 0;
        if (pal_http_req_recv(req, body + received, k_max_body - received, &got) != PAL_OK
                || got == 0) break;
        received += got;
    }
    body[received] = '\0';

    /* ── Parse envelope ──────────────────────────────────────────────────────────────────── */
    JsonDocument doc;
    if (deserializeJson(doc, body, received) != DeserializationError::Ok) {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req, "{\"ok\":false,\"error\":\"invalid JSON\"}", 0);
    }

    const char *msg_name = doc["msg"] | "";
    if (msg_name[0] == '\0') {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req, "{\"ok\":false,\"error\":\"missing msg field\"}", 0);
    }

    /* Serialise the "data" object back to a JSON string for the codec */
    char data_json[APP_MSG_CODEC_DATA_JSON_MAX + 1];
    if (!doc["data"].isNull()) {
        size_t w = serializeJson(doc["data"], data_json, sizeof(data_json));
        if (w == 0) strncpy(data_json, "{}", sizeof(data_json));
    } else {
        strncpy(data_json, "{}", sizeof(data_json));
    }

    /* ── Decode message from codec ───────────────────────────────────────────────────── */
    hsys_msg_t *msg = app_msg_codec_decode(msg_name, data_json, MODULE_ID);
    if (!msg) {
        pal_http_resp_set_status(req, 400);
        return pal_http_resp_send(req, "{\"ok\":false,\"error\":\"unknown message type\"}", 0);
    }

    /* ── Look up route ──────────────────────────────────────────────────────────────────── */
    if (!self->m_api_routes) {
        msg->ref_count = 1;
        hsys_msg_release(msg);
        pal_http_resp_set_status(req, 503);
        return pal_http_resp_send(req, "{\"ok\":false,\"error\":\"no route table\"}", 0);
    }

    const ApiMsgRouteDef *route = nullptr;
    for (const ApiMsgRouteDef *r = self->m_api_routes; r->msg_id != 0; r++) {
        if (r->msg_id == msg->msg_id) { route = r; break; }
    }
    if (!route) {
        msg->ref_count = 1;
        hsys_msg_release(msg);
        pal_http_resp_set_status(req, 403);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"message not in route table\"}", 0);
    }

    /* ── Serialise concurrent API requests ─────────────────────────────────────── */
    hsys_mutex_lock(self->m_api_lock);

    /* ── Arm the response capture and send/publish the request ───────────────── */
    if (route->response_id != 0) {
        self->m_api_wait_id = route->response_id;
    }

    hsys_status_t send_status;
    if (route->dest_module != (hsys_module_id_t)0) {
        send_status = self->send(msg, route->dest_module);
    } else {
        send_status = self->publish(msg);
    }

    if (send_status != HSYS_OK) {
        self->m_api_wait_id = 0;
        hsys_mutex_unlock(self->m_api_lock);
        pal_http_resp_set_status(req, 500);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"message send failed\"}", 0);
    }

    /* ── Fire-and-forget ──────────────────────────────────────────────────────────────────── */
    if (route->response_id == 0) {
        hsys_mutex_unlock(self->m_api_lock);
        pal_http_resp_set_type(req, "application/json");
        return pal_http_resp_send(req, "{\"ok\":true}", 0);
    }

    /* ── Wait for response (5 s timeout) ────────────────────────────────────────── */
    bool got_resp = hsys_semaphore_take_timeout(self->m_api_resp_sem, 5000);
    self->m_api_wait_id = 0;
    hsys_mutex_unlock(self->m_api_lock);

    if (!got_resp) {
        pal_http_resp_set_status(req, 504);
        return pal_http_resp_send(req,
            "{\"ok\":false,\"error\":\"response timed out\"}", 0);
    }

    /* ── Build and return JSON response ───────────────────────────────────────────── */
    char resp[APP_MSG_CODEC_DATA_JSON_MAX + APP_MSG_CODEC_MSG_NAME_MAX + 32];
    snprintf(resp, sizeof(resp),
             "{\"ok\":true,\"msg\":\"%s\",\"data\":%s}",
             self->m_api_msg_name,
             self->m_api_resp_data);

    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req, resp, 0);
}
