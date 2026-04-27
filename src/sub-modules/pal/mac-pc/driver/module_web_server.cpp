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

#include <ArduinoJson.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
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
    if (s_server_fd >= 0) {
        LOG_MSG_INFO(WEB_SRV_LOG_EN,
                     "Config web server ready: http://localhost:%d", (int)HTTP_PORT);
    }
}

void ModuleWebServer::on_msg_received(const hsys_msg_t & /*msg*/) {}

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
// HTTP listener
// =============================================================================

void ModuleWebServer::_handle_connection(int fd)
{
    // ── Parse request line ────────────────────────────────────────────────────
    char line[1024];
    if (!_read_line(fd, line, sizeof(line))) return;

    char method[16] = {}, path[256] = {};
    if (sscanf(line, "%15s %255s", method, path) != 2) return;

    // ── Consume headers; pick out Content-Length ──────────────────────────────
    int content_length = 0;
    while (_read_line(fd, line, sizeof(line)) && line[0] != '\0') {
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = atoi(line + 15);
        }
    }

    // ── Read request body (POST only) ─────────────────────────────────────────
    static constexpr int k_max_body = 4096;
    char body[k_max_body + 1];
    int  body_len = 0;

    if (strcmp(method, "POST") == 0 && content_length > 0) {
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
    const bool is_get  = strcmp(method, "GET")  == 0;
    const bool is_post = strcmp(method, "POST") == 0;

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
