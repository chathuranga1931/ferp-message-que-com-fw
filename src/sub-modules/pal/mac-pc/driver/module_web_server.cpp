/**
 * @file module_web_server.cpp
 * @brief Simulator HTTP configuration server implementation.
 *
 * Runs a simple HTTP/1.0 server on a background pthread (POSIX sockets).
 * Handles GET and POST for /api/config using ArduinoJSON for JSON merging.
 * After a successful POST, publishes MsgSpiffsReady to trigger a live
 * hot-reload through ModuleConfig.
 */

#include "module_web_server.h"
#include "pal_logger.h"
#include "msg_spiffs_ready.h"
#include "msg_ota_start_request.h"
#include "msg_ota_start_response.h"
#include "msg_ota_request_driver.h"
#include "msg_ota_driver_response.h"
#include "msg_ota_complete_notify.h"
#include "msg_ota_progress.h"
#include "FileSystemDriver.h"
#include "app_module_ids.h"

#include <ArduinoJson.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#define __TAG__        "WEBSRV__"
#ifndef WEB_SRV_LOG_EN
#define WEB_SRV_LOG_EN true
#endif

// ── SPIFFS root and config file paths (relative to simulator cwd) ────────────
static constexpr const char *k_spiffs_root  = "SPIFFS/spiffs";
static constexpr const char *k_config_path  = "SPIFFS/spiffs/Configs/DeviceConfigs.json";

// ── Static server state ───────────────────────────────────────────────────────
static int             s_server_fd = -1;
static ModuleWebServer s_instance;

// ── OTA session state (shared between listener pthread and HSYS module task) ──
static pthread_mutex_t        s_ota_mtx            = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t         s_start_resp_cond    = PTHREAD_COND_INITIALIZER;
static pthread_cond_t         s_driver_resp_cond   = PTHREAD_COND_INITIALIZER;
static volatile bool          s_ota_busy           = false;
static volatile uint32_t      s_ota_bytes_written  = 0;
static bool                   s_start_resp_ready   = false;
static ota_start_result_t     s_start_result       = OTA_START_REJECTED_BUSY;
static bool                   s_driver_resp_ready  = false;
static const ota_fs_driver_t *s_ota_driver         = nullptr;
static void                  *s_ota_ctx            = nullptr;

// ── publish_reload (public static class method) ───────────────────────────────
// Being a static method of ModuleWebServer, it can call the protected
// publish() on s_instance (derived-class object, same class scope).
bool ModuleWebServer::publish_reload()
{
    hsys_msg_t *msg = MsgSpiffsReady::create(MODULE_ID);
    if (!msg) return false;
    s_instance.publish(msg);   // protected, but accessible here (class scope)
    return true;
}

// ── OTA static wrappers ──────────────────────────────────────────────────────
// Being static methods of ModuleWebServer, these can call the protected
// send() / publish() on s_instance from inside the HTTP listener pthread.

bool ModuleWebServer::send_ota_start_request(uint8_t target_idx, const char *version)
{
    MsgOtaStartRequest::Payload p{};
    p.target_idx = target_idx;
    strncpy(p.incoming_version, version, sizeof(p.incoming_version) - 1);
    auto *msg = MsgOtaStartRequest::create(MODULE_ID, p);
    if (!msg) return false;
    s_instance.send(msg, MODULE_OTA_ID);
    return true;
}

bool ModuleWebServer::send_ota_request_driver()
{
    auto *msg = MsgOtaRequestDriver::create(MODULE_ID);
    if (!msg) return false;
    s_instance.send(msg, MODULE_OTA_ID);
    return true;
}

bool ModuleWebServer::publish_ota_progress(uint8_t target_idx,
                                            uint32_t bytes_written, uint32_t total_bytes)
{
    MsgOtaProgress::Payload p{};
    p.target_idx    = target_idx;
    p.bytes_written = bytes_written;
    p.total_bytes   = total_bytes;
    p.percent       = (total_bytes > 0)
                      ? (uint8_t)((bytes_written * 100u) / total_bytes)
                      : 0;
    auto *msg = MsgOtaProgress::create(MODULE_ID, p);
    if (!msg) return false;
    s_instance.publish(msg);
    return true;
}

bool ModuleWebServer::send_ota_complete_notify(bool success)
{
    MsgOtaCompleteNotify::Payload p{};
    p.success    = success;
    p.last_error = success ? OTA_FS_OK : OTA_FS_ERR_WRITE_FAIL;
    auto *msg = MsgOtaCompleteNotify::create(MODULE_ID, p);
    if (!msg) return false;
    s_instance.send(msg, MODULE_OTA_ID);
    return true;
}

// ── Module instance ───────────────────────────────────────────────────────────
ModuleWebServer *ModuleWebServer::instance() { return &s_instance; }

// =============================================================================
// Lifecycle
// =============================================================================

void ModuleWebServer::pre_init()
{
    // ── 1. Create server socket ───────────────────────────────────────────────
    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) {
        LOG_MSG_ERROR(WEB_SRV_LOG_EN, "socket() failed: %s", strerror(errno));
        return;
    }

    int opt = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(HTTP_PORT);

    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_MSG_ERROR(WEB_SRV_LOG_EN, "bind(:%d) failed: %s",
                      (int)HTTP_PORT, strerror(errno));
        close(s_server_fd);
        s_server_fd = -1;
        return;
    }

    listen(s_server_fd, 8);

    // ── 2. Start background listener thread (detached) ────────────────────────
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t tid;
    pthread_create(&tid, &attr, _listener_thread, nullptr);
    pthread_attr_destroy(&attr);
}

void ModuleWebServer::init()
{
    // Subscribe to OTA responses so on_msg_received() can relay them to the
    // listener pthread via the condvar handshake.
    subscribe(MsgOtaStartResponse::ID);
    subscribe(MsgOtaDriverResponse::ID);

    if (s_server_fd >= 0) {
        LOG_MSG_INFO(WEB_SRV_LOG_EN,
                     "Config+OTA web server ready: http://localhost:%d", (int)HTTP_PORT);
    }
}

void ModuleWebServer::on_msg_received(const hsys_msg_t &msg)
{
    // Relay OTA handshake responses to the waiting listener pthread.
    switch (msg.msg_id) {

        case MsgOtaStartResponse::ID: {
            auto p = MsgOtaStartResponse::deserialize(msg);
            pthread_mutex_lock(&s_ota_mtx);
            s_start_result    = p.result;
            s_start_resp_ready = true;
            pthread_cond_signal(&s_start_resp_cond);
            pthread_mutex_unlock(&s_ota_mtx);
            LOG_MSG_DEBUG(WEB_SRV_LOG_EN, "OTA start response: result=%d", (int)p.result);
            break;
        }

        case MsgOtaDriverResponse::ID: {
            auto p = MsgOtaDriverResponse::deserialize(msg);
            pthread_mutex_lock(&s_ota_mtx);
            s_ota_driver        = p.driver;
            s_ota_ctx           = p.ctx;
            s_driver_resp_ready = true;
            pthread_cond_signal(&s_driver_resp_cond);
            pthread_mutex_unlock(&s_ota_mtx);
            LOG_MSG_DEBUG(WEB_SRV_LOG_EN, "OTA driver response: driver=%p ctx=%p",
                          (void *)p.driver, p.ctx);
            break;
        }

        default:
            break;
    }
}

// =============================================================================
// Private helpers (file-scope)
// =============================================================================

/** Portable mkdir -p (only the leading dirs need to exist; creates each dir). */
static void _mkdirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/** Read one line from fd, stripping the trailing CR/LF.
 *  Returns false when the connection is closed or an error occurs. */
static bool _read_line(int fd, char *buf, size_t max_len)
{
    size_t n = 0;
    while (n < max_len - 1) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) return false;
        if (c == '\r') continue;        // skip CR (CRLF line endings)
        if (c == '\n') break;           // end of line
        buf[n++] = c;
    }
    buf[n] = '\0';
    return true;
}

/** Send a complete HTTP response. */
static void _send_response(int fd, int code,
                            const char *content_type,
                            const char *body, size_t body_len)
{
    const char *reason = (code == 200) ? "OK"
                       : (code == 400) ? "Bad Request"
                       : (code == 404) ? "Not Found"
                                       : "Internal Server Error";
    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        code, reason, content_type, body_len);
    send(fd, header, (size_t)hlen, 0);
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}

static void _send_json(int fd, const char *json, int code = 200)
{
    _send_response(fd, code, "application/json", json, strlen(json));
}

/** Return MIME type for common web file extensions. */
static const char *_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".htm")  == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".jpg")  == 0) return "image/jpeg";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".txt")  == 0) return "text/plain";
    return "application/octet-stream";
}

/** Serve a file from the SPIFFS emulation directory.
 *  @p rel_path  path relative to SPIFFS root, e.g. "index.html" */
static void _serve_spiffs_file(int fd, const char *rel_path)
{
    char full[512];
    snprintf(full, sizeof(full), "%s/%s", k_spiffs_root, rel_path);

    FILE *f = fopen(full, "rb");
    if (!f) {
        LOG_MSG_WARNING(WEB_SRV_LOG_EN, "file not found: %s", full);
        _send_json(fd, "{\"error\":\"file not found\"}", 404);
        return;
    }

    // Determine file size
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Send header first (no Content-Length in chunked streaming, use HTTP/1.0
    // close-after-response so browser knows when body ends).
    const char *mime = _mime_type(rel_path);
    char header[256];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "\r\n",
        mime, sz);
    send(fd, header, (size_t)hlen, 0);

    // Stream file in 4 KB chunks
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        send(fd, buf, n, 0);
    }
    fclose(f);
}

// =============================================================================
// Request handlers
// =============================================================================

static void _handle_get_config(int fd)
{
    char buf[4096];
    FILE *f = fopen(k_config_path, "r");
    if (!f) {
        _send_json(fd, "{}");
        return;
    }
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    _send_response(fd, 200, "application/json", buf, n);
}

static void _handle_post_config(int fd, const char *body, int body_len)
{
    // ── 1. Parse incoming JSON ────────────────────────────────────────────────
    JsonDocument new_doc;
    DeserializationError err = deserializeJson(new_doc, body, (size_t)body_len);
    if (err != DeserializationError::Ok) {
        _send_json(fd, "{\"ok\":false,\"error\":\"invalid JSON body\"}", 400);
        return;
    }

    // ── 2. Read existing config (fall back to empty object if missing) ─────────
    char existing_json[4096] = "{}";
    FILE *f = fopen(k_config_path, "r");
    if (f) {
        size_t n = fread(existing_json, 1, sizeof(existing_json) - 1, f);
        fclose(f);
        existing_json[n] = '\0';
    }
    JsonDocument existing_doc;
    deserializeJson(existing_doc, existing_json);

    // ── 3. Merge: new values overwrite matching keys in the existing doc ───────
    for (JsonPair kv : new_doc.as<JsonObject>()) {
        existing_doc[kv.key()] = kv.value();
    }

    // ── 4. Write merged config back to disk ───────────────────────────────────
    _mkdirs("SPIFFS/spiffs/Configs");
    f = fopen(k_config_path, "w");
    if (!f) {
        _send_json(fd, "{\"ok\":false,\"error\":\"write failed\"}", 500);
        return;
    }
    char out_buf[4096];
    size_t written = serializeJsonPretty(existing_doc, out_buf, sizeof(out_buf));
    fwrite(out_buf, 1, written, f);
    fclose(f);

    // ── 5. Hot-reload: trigger ModuleConfig to re-read the file ───────────────
    //
    // publish_reload() is a static class method so it can access the
    // protected publish() from inside the ModuleWebServer class scope,
    // even though this free function is called from the listener pthread.
    bool hot_reload = ModuleWebServer::publish_reload();

    _send_json(fd, hot_reload
        ? "{\"ok\":true,\"hot_reload\":true}"
        : "{\"ok\":true,\"hot_reload\":false}");
}

// =============================================================================
// OTA status handler
// =============================================================================

void ModuleWebServer::_handle_get_ota_status(int fd)
{
    pthread_mutex_lock(&s_ota_mtx);
    bool     busy  = s_ota_busy;
    uint32_t bytes = s_ota_bytes_written;
    pthread_mutex_unlock(&s_ota_mtx);

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"state\":\"%s\",\"bytes\":%u}",
             busy ? "uploading" : "idle",
             (unsigned)bytes);
    _send_json(fd, resp);
}

// =============================================================================
// Firmware upload handler  (POST /updateFirmwareBin)
// =============================================================================

/** Portable memmem — finds needle inside haystack without requiring libc extension. */
static const void *_find_mem(const void *haystack, size_t hlen,
                              const void *needle, size_t nlen)
{
    if (nlen == 0) return haystack;
    const char *h = (const char *)haystack;
    const char *n = (const char *)needle;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(h + i, n, nlen) == 0) return h + i;
    }
    return nullptr;
}

void ModuleWebServer::_handle_post_firmware(int fd, int content_length,
                                             const char *content_type_hdr)
{
    // ── 1. Extract multipart boundary ─────────────────────────────────────────
    const char *bp = strstr(content_type_hdr, "boundary=");
    if (!bp) {
        _send_json(fd, "{\"ok\":false,\"error\":\"missing multipart boundary\"}", 400);
        return;
    }
    char boundary[256] = {};
    strncpy(boundary, bp + 9, sizeof(boundary) - 1);
    for (char *p = boundary; *p; p++) {
        if (*p == ' ' || *p == '\r' || *p == '\n') { *p = '\0'; break; }
    }
    if (boundary[0] == '\0') {
        _send_json(fd, "{\"ok\":false,\"error\":\"empty boundary\"}", 400);
        return;
    }

    // ── 2. Guard against concurrent uploads ───────────────────────────────────
    pthread_mutex_lock(&s_ota_mtx);
    if (s_ota_busy) {
        pthread_mutex_unlock(&s_ota_mtx);
        _send_json(fd, "{\"ok\":false,\"error\":\"ota already in progress\"}", 503);
        return;
    }
    s_ota_busy          = true;
    s_ota_bytes_written = 0;
    s_start_resp_ready  = false;
    s_driver_resp_ready = false;
    pthread_mutex_unlock(&s_ota_mtx);

    // Helper: release busy flag and send error
    auto _fail = [&](const char *json_err, int http_code) {
        pthread_mutex_lock(&s_ota_mtx);
        s_ota_busy = false;
        pthread_mutex_unlock(&s_ota_mtx);
        _send_json(fd, json_err, http_code);
    };

    // ── 3. Validate body size (firmware is bounded to 4 MB) ───────────────────
    static constexpr int k_max_fw = 4 * 1024 * 1024;
    if (content_length <= 0 || content_length > k_max_fw) {
        _fail("{\"ok\":false,\"error\":\"invalid content-length\"}", 400);
        return;
    }

    // ── 4. Send MsgOtaStartRequest → OtaModule ────────────────────────────────
    LOG_MSG_INFO(WEB_SRV_LOG_EN, "OTA: requesting session from OtaModule");
    if (!send_ota_start_request(0, "webserver-upload")) {
        _fail("{\"ok\":false,\"error\":\"failed to create start-request message\"}", 500);
        return;
    }

    // ── 5. Wait for MsgOtaStartResponse (5 s timeout) ────────────────────────
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    pthread_mutex_lock(&s_ota_mtx);
    while (!s_start_resp_ready) {
        if (pthread_cond_timedwait(&s_start_resp_cond, &s_ota_mtx, &ts) != 0) break;
    }
    bool accepted = s_start_resp_ready && (s_start_result == OTA_START_ACCEPTED);
    ota_start_result_t result = s_start_result;
    pthread_mutex_unlock(&s_ota_mtx);

    if (!accepted) {
        LOG_MSG_WARNING(WEB_SRV_LOG_EN, "OTA: start rejected (result=%d)", (int)result);
        _fail("{\"ok\":false,\"error\":\"ota start rejected\"}", 503);
        return;
    }
    LOG_MSG_INFO(WEB_SRV_LOG_EN, "OTA: session accepted — requesting driver");

    // ── 6. Send MsgOtaRequestDriver → OtaModule ───────────────────────────────
    if (!send_ota_request_driver()) {
        _fail("{\"ok\":false,\"error\":\"failed to create driver-request message\"}", 500);
        return;
    }

    // ── 7. Wait for MsgOtaDriverResponse (5 s timeout) ───────────────────────
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;

    pthread_mutex_lock(&s_ota_mtx);
    while (!s_driver_resp_ready) {
        if (pthread_cond_timedwait(&s_driver_resp_cond, &s_ota_mtx, &ts) != 0) break;
    }
    const ota_fs_driver_t *drv = s_ota_driver;
    void                  *ctx = s_ota_ctx;
    bool drv_ok = s_driver_resp_ready && (drv != nullptr);
    pthread_mutex_unlock(&s_ota_mtx);

    if (!drv_ok) {
        LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA: driver response timed-out or null");
        _fail("{\"ok\":false,\"error\":\"ota driver not available\"}", 500);
        return;
    }
    LOG_MSG_INFO(WEB_SRV_LOG_EN, "OTA: driver acquired — reading body (%d B)",
                 content_length);

    // ── 8. Read full multipart body into heap buffer ──────────────────────────
    char *body = (char *)malloc((size_t)content_length + 1);
    if (!body) {
        _fail("{\"ok\":false,\"error\":\"out of memory\"}", 500);
        return;
    }

    int total_read = 0;
    while (total_read < content_length) {
        ssize_t r = recv(fd, body + total_read,
                         (size_t)(content_length - total_read), 0);
        if (r <= 0) break;
        total_read += (int)r;
    }
    body[total_read] = '\0';

    if (total_read < content_length) {
        free(body);
        _fail("{\"ok\":false,\"error\":\"connection closed mid-upload\"}", 400);
        return;
    }

    // ── 9. Parse multipart — locate raw binary data ───────────────────────────
    // Body structure:
    //   --BOUNDARY\r\n
    //   Content-Disposition: ...\r\n
    //   ...\r\n
    //   \r\n
    //   <BINARY DATA>
    //   \r\n--BOUNDARY--\r\n

    char bound_open[264];   // "--" + boundary
    char bound_close[270];  // "\r\n--" + boundary  (end-of-part marker)
    snprintf(bound_open,  sizeof(bound_open),  "--%s",     boundary);
    snprintf(bound_close, sizeof(bound_close), "\r\n--%s", boundary);

    // Find opening boundary
    const char *part = (const char *)_find_mem(body, (size_t)total_read,
                                               bound_open, strlen(bound_open));
    if (!part) {
        free(body);
        _fail("{\"ok\":false,\"error\":\"multipart boundary not found\"}", 400);
        return;
    }

    // Skip boundary line (to \r\n)
    const char *hdr_start = (const char *)_find_mem(part, (size_t)(total_read - (part - body)),
                                                    "\r\n", 2);
    if (!hdr_start) { free(body); _fail("{\"ok\":false,\"error\":\"malformed multipart\"}", 400); return; }
    hdr_start += 2;

    // Skip part headers (find \r\n\r\n)
    const char *bin_start = (const char *)_find_mem(hdr_start,
                                                    (size_t)(total_read - (hdr_start - body)),
                                                    "\r\n\r\n", 4);
    if (!bin_start) { free(body); _fail("{\"ok\":false,\"error\":\"no part header end\"}", 400); return; }
    bin_start += 4;

    // Find end of binary data (\r\n--BOUNDARY)
    size_t bin_search_len = (size_t)(total_read - (bin_start - body));
    const char *bin_end = (const char *)_find_mem(bin_start, bin_search_len,
                                                  bound_close, strlen(bound_close));
    if (!bin_end) { free(body); _fail("{\"ok\":false,\"error\":\"end boundary not found\"}", 400); return; }

    uint32_t bin_len = (uint32_t)(bin_end - bin_start);
    LOG_MSG_INFO(WEB_SRV_LOG_EN, "OTA: binary payload %u B — starting write", (unsigned)bin_len);

    // ── 10. Stream binary data through the OTA driver ─────────────────────────
    ota_fs_err_t err = drv->fopen(ctx, "firmware.bin", OTA_FS_OPEN_WRITE);
    if (err != OTA_FS_OK) {
        free(body);
        send_ota_complete_notify(false);
        _fail("{\"ok\":false,\"error\":\"driver fopen failed\"}", 500);
        return;
    }

    static constexpr uint32_t k_chunk = 4096;
    uint32_t offset   = 0;
    bool     write_ok = true;

    while (offset < bin_len && write_ok) {
        uint32_t sz = ((bin_len - offset) < k_chunk) ? (bin_len - offset) : k_chunk;
        err = drv->fwrite(ctx, (const uint8_t *)(bin_start + offset), sz);
        if (err != OTA_FS_OK) {
            LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA: fwrite failed at offset %u (err=%d)",
                          (unsigned)offset, (int)err);
            write_ok = false;
            break;
        }
        offset += sz;

        // Update visible byte counter
        pthread_mutex_lock(&s_ota_mtx);
        s_ota_bytes_written = offset;
        pthread_mutex_unlock(&s_ota_mtx);

        // Publish progress notification (resets OtaModule inactivity timer)
        publish_ota_progress(0, offset, bin_len);
    }

    if (write_ok) {
        err = drv->fclose(ctx);
        write_ok = (err == OTA_FS_OK);
        if (!write_ok) {
            LOG_MSG_ERROR(WEB_SRV_LOG_EN, "OTA: fclose failed (err=%d)", (int)err);
        }
    } else {
        drv->ferase(ctx);   // clean up partial write
    }

    free(body);

    // ── 11. Notify OtaModule of outcome ───────────────────────────────────────
    send_ota_complete_notify(write_ok);

    pthread_mutex_lock(&s_ota_mtx);
    s_ota_busy = false;
    pthread_mutex_unlock(&s_ota_mtx);

    if (write_ok) {
        LOG_MSG_INFO(WEB_SRV_LOG_EN, "OTA: complete — %u B written", (unsigned)offset);
        char resp[128];
        snprintf(resp, sizeof(resp), "{\"ok\":true,\"bytes\":%u}", (unsigned)offset);
        _send_json(fd, resp);
    } else {
        _send_json(fd, "{\"ok\":false,\"error\":\"write failed\"}", 500);
    }
}

// =============================================================================
// HTTP listener
// =============================================================================

void ModuleWebServer::_handle_connection(int fd)
{
    // ── Parse request line ────────────────────────────────────────────────────
    char line[1024];
    if (!_read_line(fd, line, sizeof(line))) return;

    char method[16] = {}, path[256] = {};
    if (sscanf(line, "%15s %255s", method, path) != 2) return;

    // ── Consume headers; pick out Content-Length and Content-Type ───────────────
    int  content_length = 0;
    char content_type[256] = {};
    while (_read_line(fd, line, sizeof(line)) && line[0] != '\0') {
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = atoi(line + 15);
        } else if (strncasecmp(line, "Content-Type:", 13) == 0) {
            // Copy and trim leading whitespace
            const char *ct = line + 13;
            while (*ct == ' ') ct++;
            strncpy(content_type, ct, sizeof(content_type) - 1);
        }
    }

    // ── Firmware upload: route before body read to support streaming ──────────
    const bool is_get  = strcmp(method, "GET")  == 0;
    const bool is_post = strcmp(method, "POST") == 0;

    if (is_post && strcmp(path, "/updateFirmwareBin") == 0) {
        _handle_post_firmware(fd, content_length, content_type);
        return;
    }

    // ── Read request body (POST only, for all other endpoints) ────────────────
    static constexpr int k_max_body = 4096;
    char body[k_max_body + 1];
    int  body_len = 0;

    if (is_post && content_length > 0) {
        body_len = (content_length < k_max_body) ? content_length : k_max_body;
        int total = 0;
        while (total < body_len) {
            ssize_t r = recv(fd, body + total, (size_t)(body_len - total), 0);
            if (r <= 0) break;
            total += (int)r;
        }
        body_len  = total;
        body[total] = '\0';
    } else {
        body[0] = '\0';
    }

    // ── Route ─────────────────────────────────────────────────────────────────
    // Matches the same URLs as the old app_webserver (ESP32 side), plus
    // /api/* aliases for shell-script curl access.

    // ── Static files from SPIFFS/spiffs/ ──────────────────────────────────────
    if (is_get && (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        _serve_spiffs_file(fd, "index.html");
    }
    else if (is_get && strcmp(path, "/styles.css") == 0) {
        _serve_spiffs_file(fd, "styles.css");
    }
    else if (is_get && strcmp(path, "/deviceConfigurations") == 0) {
        _serve_spiffs_file(fd, "deviceConfigurations.html");
    }
    // ── Config API — old-app URLs ─────────────────────────────────────────────
    else if (is_get  && strcmp(path, "/getDeviceConfigurations") == 0) {
        _handle_get_config(fd);
    }
    else if (is_post && strcmp(path, "/setDeviceConfigurationsPost") == 0) {
        _handle_post_config(fd, body, body_len);
    }
    // ── Config API — curl-friendly aliases ───────────────────────────────────
    else if (is_get  && strcmp(path, "/api/config") == 0) {
        _handle_get_config(fd);
    }
    else if (is_post && strcmp(path, "/api/config") == 0) {
        _handle_post_config(fd, body, body_len);
    }
    else if (is_get  && strcmp(path, "/api/status") == 0) {
        _send_json(fd, "{\"running\":true,\"port\":8080}");
    }
    // ── OTA API ───────────────────────────────────────────────────────────────
    else if (is_get  && strcmp(path, "/api/ota/status") == 0) {
        _handle_get_ota_status(fd);
    }
    else {
        _send_json(fd, "{\"error\":\"not found\"}", 404);
    }
}

void *ModuleWebServer::_listener_thread(void * /*arg*/)
{
    if (s_server_fd < 0) return nullptr;

    LOG_MSG_DEBUG(WEB_SRV_LOG_EN, "listener started on port %d", (int)HTTP_PORT);

    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(s_server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        _handle_connection(client_fd);
        close(client_fd);
    }
    return nullptr;
}
