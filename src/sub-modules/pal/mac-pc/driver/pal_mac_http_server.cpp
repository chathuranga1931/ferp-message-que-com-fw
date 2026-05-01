/**
 * @file pal_mac_http_server.cpp
 * @brief PAL HTTP Server implementation for macOS/Linux (POSIX sockets).
 *
 * Implements pal_http_server.h using a background pthread + a simple
 * HTTP/1.0 request dispatcher.  Designed to mirror the esp_http_server
 * semantics used by pal_esp_idf_http_server.cpp so that ModuleWebServer
 * can be built unchanged on both platforms.
 *
 * Listener model:
 *   - pal_http_server_start()  → creates server socket, spawns listener pthread
 *   - pal_http_server_register_uri*() → adds entries to a dispatch table
 *   - Listener pthread accepts() connections, parses HTTP/1.0 request,
 *     dispatches to the matching handler, then closes the connection.
 *   - Upload POST endpoints: read full body, parse multipart, call
 *     upload_handler once with (offset=0, is_final=true, full binary).
 *
 * Limitations:
 *   - One connection is handled at a time per server (no threading per conn).
 *   - HTTP/1.0 only (no keep-alive, no chunked encoding).
 *   - pal_ota_begin/write/end/abort are stubs — ModuleWebServer uses the
 *     HSYS OTA message protocol, not these direct PAL functions.
 */

#include "pal_http_server.h"
#include "pal_spiffs.h"
#include "pal_logger.h"
#include "pal_types.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#define __TAG__  "PAL_HTTP"

#ifndef HTTP_SRV_LOG_EN
#define HTTP_SRV_LOG_EN true
#endif

/*===========================================================================*/
/*                       INTERNAL TYPES                                      */
/*===========================================================================*/

#define MAC_MAX_HANDLERS        32
#define MAC_MAX_STORED_HEADERS  24

typedef struct {
    char name[64];
    char value[256];
} mac_http_header_t;

/** Opaque request context passed to handler callbacks as pal_http_request_t. */
typedef struct {
    int                  client_fd;
    char                 method[16];
    char                 path[256];
    char                 query[512];
    int                  content_length;
    mac_http_header_t    headers[MAC_MAX_STORED_HEADERS];
    int                  header_count;
    /* Response state — buffered until pal_http_resp_send*() is called */
    int                  resp_status_code;
    char                 resp_content_type[64];
    bool                 header_sent;           /* true after first chunk sent */
} mac_req_ctx_t;

typedef struct {
    char                      uri[256];
    pal_http_method_t         method;
    pal_http_uri_handler_t    handler;
    pal_http_upload_handler_t upload_handler;    /* NULL for normal endpoints */
    void                     *user_ctx;
} mac_uri_entry_t;

typedef struct {
    int              server_fd;
    mac_uri_entry_t  entries[MAC_MAX_HANDLERS];
    int              entry_count;
    pthread_mutex_t  lock;
    pthread_t        listener_tid;
} mac_http_server_t;

/*===========================================================================*/
/*                        PRIVATE HELPERS                                    */
/*===========================================================================*/

/** Read one CRLF-terminated line from socket into buf (max_len-1 chars + NUL). */
static bool _read_line(int fd, char *buf, size_t max_len)
{
    size_t n = 0;
    while (n < max_len - 1) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) return false;
        if (c == '\r') continue;
        if (c == '\n') break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return true;
}

static const char *_status_reason(int code)
{
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default:  return "OK";
    }
}

static const char *_get_header(const mac_req_ctx_t *r, const char *name)
{
    for (int i = 0; i < r->header_count; i++) {
        if (strcasecmp(r->headers[i].name, name) == 0)
            return r->headers[i].value;
    }
    return nullptr;
}

static pal_http_method_t _str_to_method(const char *s)
{
    if (strcmp(s, "POST")   == 0) return PAL_HTTP_POST;
    if (strcmp(s, "PUT")    == 0) return PAL_HTTP_PUT;
    if (strcmp(s, "DELETE") == 0) return PAL_HTTP_DELETE;
    if (strcmp(s, "HEAD")   == 0) return PAL_HTTP_HEAD;
    return PAL_HTTP_GET;
}

static const char *_get_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".js")   == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".jpg")  == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".txt")  == 0) return "text/plain";
    return "application/octet-stream";
}

/** Portable memmem (haystack/needle search). */
static const void *_memmem(const void *h, size_t hn, const void *n, size_t nn)
{
    if (nn == 0) return h;
    const char *hp = (const char *)h;
    const char *np = (const char *)n;
    for (size_t i = 0; i + nn <= hn; i++) {
        if (memcmp(hp + i, np, nn) == 0) return hp + i;
    }
    return nullptr;
}

/*===========================================================================*/
/*                    UPLOAD (MULTIPART) HANDLING                            */
/*===========================================================================*/

static void _handle_upload(mac_uri_entry_t *entry, mac_req_ctx_t *req_ctx)
{
    int client_fd = req_ctx->client_fd;

    if (req_ctx->content_length <= 0) {
        pal_http_resp_set_status((pal_http_request_t)req_ctx, 400);
        pal_http_resp_send((pal_http_request_t)req_ctx,
                           "{\"ok\":false,\"error\":\"no content\"}", 0);
        return;
    }

    static constexpr int k_max_body = 4 * 1024 * 1024;
    if (req_ctx->content_length > k_max_body) {
        pal_http_resp_set_status((pal_http_request_t)req_ctx, 400);
        pal_http_resp_send((pal_http_request_t)req_ctx,
                           "{\"ok\":false,\"error\":\"body too large\"}", 0);
        return;
    }

    /* Read full body into malloc'd buffer */
    char *body = (char *)malloc((size_t)req_ctx->content_length + 1);
    if (!body) {
        pal_http_resp_set_status((pal_http_request_t)req_ctx, 500);
        pal_http_resp_send((pal_http_request_t)req_ctx,
                           "{\"ok\":false,\"error\":\"out of memory\"}", 0);
        return;
    }

    int total = 0;
    while (total < req_ctx->content_length) {
        ssize_t n = recv(client_fd, body + total,
                         (size_t)(req_ctx->content_length - total), 0);
        if (n <= 0) break;
        total += (int)n;
    }
    body[total] = '\0';

    if (total < req_ctx->content_length) {
        free(body);
        pal_http_resp_set_status((pal_http_request_t)req_ctx, 400);
        pal_http_resp_send((pal_http_request_t)req_ctx,
                           "{\"ok\":false,\"error\":\"connection dropped\"}", 0);
        return;
    }

    /* Parse multipart boundary from Content-Type header */
    const char *ct = _get_header(req_ctx, "Content-Type");
    char boundary[256] = {};
    bool is_multipart = false;

    if (ct) {
        const char *bp = strstr(ct, "boundary=");
        if (bp) {
            strncpy(boundary, bp + 9, sizeof(boundary) - 3);
            for (char *p = boundary; *p; p++) {
                if (*p == ' ' || *p == '\r' || *p == '\n') { *p = '\0'; break; }
            }
            is_multipart = (boundary[0] != '\0');
        }
    }

    const uint8_t *bin_data = (const uint8_t *)body;
    size_t         bin_len  = (size_t)total;
    char           filename[128] = "firmware.bin";

    if (is_multipart) {
        char bound_open[260];
        char bound_close[270];
        snprintf(bound_open,  sizeof(bound_open),  "--%s",     boundary);
        snprintf(bound_close, sizeof(bound_close), "\r\n--%s", boundary);

        const char *part = (const char *)_memmem(body, (size_t)total,
                                                  bound_open, strlen(bound_open));
        if (!part) {
            free(body);
            pal_http_resp_set_status((pal_http_request_t)req_ctx, 400);
            pal_http_resp_send((pal_http_request_t)req_ctx,
                               "{\"ok\":false,\"error\":\"boundary not found\"}", 0);
            return;
        }

        /* Skip past boundary line to part headers */
        size_t remaining = (size_t)(total - (part - body));
        const char *hdr_start = (const char *)_memmem(part, remaining, "\r\n", 2);
        if (!hdr_start) { free(body); return; }
        hdr_start += 2;

        /* Extract filename from Content-Disposition in part headers */
        size_t hdr_remaining = (size_t)(total - (hdr_start - body));
        const char *cd = (const char *)_memmem(hdr_start, hdr_remaining,
                                                "Content-Disposition:", 20);
        if (cd) {
            const char *fn = strstr(cd, "filename=");
            if (fn) {
                fn += 9;
                if (*fn == '"') fn++;
                size_t fn_len = strcspn(fn, "\"\r\n");
                if (fn_len > 0 && fn_len < sizeof(filename) - 1) {
                    strncpy(filename, fn, fn_len);
                    filename[fn_len] = '\0';
                }
            }
        }

        /* Find end of part headers (\r\n\r\n) */
        const char *bin_start = (const char *)_memmem(hdr_start, hdr_remaining,
                                                       "\r\n\r\n", 4);
        if (!bin_start) { free(body); return; }
        bin_start += 4;

        /* Find closing boundary (\r\n--BOUNDARY) */
        size_t bin_search = (size_t)(total - (bin_start - body));
        const char *bin_end = (const char *)_memmem(bin_start, bin_search,
                                                     bound_close, strlen(bound_close));
        if (!bin_end) { free(body); return; }

        bin_data = (const uint8_t *)bin_start;
        bin_len  = (size_t)(bin_end - bin_start);
        LOG_MSG_DEBUG(HTTP_SRV_LOG_EN, "Upload: file='%s' binary=%zu B", filename, bin_len);
    }

    /* Call upload handler once with full payload (is_final = true) */
    entry->upload_handler((pal_http_request_t)req_ctx, filename,
                          0, bin_data, bin_len, true, entry->user_ctx);

    free(body);

    /* Call completion handler if registered */
    if (entry->handler) {
        entry->handler((pal_http_request_t)req_ctx, entry->user_ctx);
    }
}

/*===========================================================================*/
/*                     CONNECTION DISPATCHER                                 */
/*===========================================================================*/

static void _handle_connection(mac_http_server_t *srv, int client_fd)
{
    mac_req_ctx_t req = {};
    req.client_fd         = client_fd;
    req.resp_status_code  = 200;
    req.header_sent       = false;
    strncpy(req.resp_content_type, "application/json",
            sizeof(req.resp_content_type) - 1);

    /* Parse request line */
    char line[1024];
    if (!_read_line(client_fd, line, sizeof(line))) return;

    char full_path[512] = {};
    if (sscanf(line, "%15s %511s", req.method, full_path) != 2) return;

    /* Split path and query string */
    char *q = strchr(full_path, '?');
    if (q) {
        size_t path_len = (size_t)(q - full_path);
        strncpy(req.path, full_path,
                path_len < sizeof(req.path) - 1 ? path_len : sizeof(req.path) - 1);
        strncpy(req.query, q + 1, sizeof(req.query) - 1);
    } else {
        strncpy(req.path, full_path, sizeof(req.path) - 1);
    }

    /* Parse headers */
    while (_read_line(client_fd, line, sizeof(line)) && line[0] != '\0') {
        char *colon = strchr(line, ':');
        if (colon && req.header_count < MAC_MAX_STORED_HEADERS) {
            size_t name_len = (size_t)(colon - line);
            strncpy(req.headers[req.header_count].name, line,
                    name_len < 63 ? name_len : 63);
            const char *val = colon + 1;
            while (*val == ' ') val++;
            strncpy(req.headers[req.header_count].value, val,
                    sizeof(req.headers[0].value) - 1);
            req.header_count++;
        }
    }

    /* Extract Content-Length */
    const char *cl = _get_header(&req, "Content-Length");
    if (cl) req.content_length = atoi(cl);

    pal_http_method_t pal_method = _str_to_method(req.method);

    /* Dispatch to registered handler */
    pthread_mutex_lock(&srv->lock);
    int entry_count = srv->entry_count;
    mac_uri_entry_t entries_copy[MAC_MAX_HANDLERS];
    memcpy(entries_copy, srv->entries, sizeof(mac_uri_entry_t) * (size_t)entry_count);
    pthread_mutex_unlock(&srv->lock);

    for (int i = 0; i < entry_count; i++) {
        mac_uri_entry_t *e = &entries_copy[i];
        if (strcmp(e->uri, req.path) != 0) continue;
        if (e->method != pal_method) continue;

        if (e->upload_handler) {
            _handle_upload(e, &req);
        } else {
            e->handler((pal_http_request_t)&req, e->user_ctx);
        }
        return;
    }

    /* No handler matched */
    LOG_MSG_DEBUG(HTTP_SRV_LOG_EN, "404: %s %s", req.method, req.path);
    pal_http_resp_send_404((pal_http_request_t)&req);
}

/*===========================================================================*/
/*                      LISTENER PTHREAD                                     */
/*===========================================================================*/

static void *_listener_thread(void *arg)
{
    mac_http_server_t *srv = (mac_http_server_t *)arg;
    LOG_MSG_INFO(HTTP_SRV_LOG_EN, "HTTP listener started (fd=%d)", srv->server_fd);

    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(srv->server_fd,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        _handle_connection(srv, client_fd);
        close(client_fd);
    }

    LOG_MSG_INFO(HTTP_SRV_LOG_EN, "HTTP listener stopped");
    return nullptr;
}

/*===========================================================================*/
/*                     SERVER OPERATIONS                                     */
/*===========================================================================*/

int32_t pal_http_server_start(const pal_http_server_config_t *config,
                               pal_http_server_handle_t *server_handle)
{
    if (!server_handle) return PAL_ERROR_INVALID;

    mac_http_server_t *srv =
        (mac_http_server_t *)calloc(1, sizeof(mac_http_server_t));
    if (!srv) return PAL_ERROR_NO_MEMORY;

    pthread_mutex_init(&srv->lock, nullptr);

    uint16_t port = config ? config->port : 80;

    srv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->server_fd < 0) {
        pthread_mutex_destroy(&srv->lock);
        free(srv);
        return PAL_ERROR_INIT;
    }

    int opt = 1;
    setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(srv->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_MSG_ERROR(HTTP_SRV_LOG_EN, "bind(:%d) failed: %s",
                      (int)port, strerror(errno));
        close(srv->server_fd);
        pthread_mutex_destroy(&srv->lock);
        free(srv);
        return PAL_ERROR_INIT;
    }

    if (listen(srv->server_fd, 8) < 0) {
        close(srv->server_fd);
        pthread_mutex_destroy(&srv->lock);
        free(srv);
        return PAL_ERROR_INIT;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&srv->listener_tid, &attr, _listener_thread, srv);
    pthread_attr_destroy(&attr);

    *server_handle = (pal_http_server_handle_t)srv;
    LOG_MSG_INFO(HTTP_SRV_LOG_EN, "HTTP server started on port %d", (int)port);
    return PAL_OK;
}

int32_t pal_http_server_stop(pal_http_server_handle_t server_handle)
{
    if (!server_handle) return PAL_ERROR_INVALID;
    mac_http_server_t *srv = (mac_http_server_t *)server_handle;
    close(srv->server_fd);
    srv->server_fd = -1;
    return PAL_OK;
}

int32_t pal_http_server_register_uri(pal_http_server_handle_t server_handle,
                                      const char *uri,
                                      pal_http_method_t method,
                                      pal_http_uri_handler_t handler,
                                      void *user_ctx)
{
    if (!server_handle || !uri || !handler) return PAL_ERROR_INVALID;
    mac_http_server_t *srv = (mac_http_server_t *)server_handle;

    pthread_mutex_lock(&srv->lock);
    if (srv->entry_count >= MAC_MAX_HANDLERS) {
        pthread_mutex_unlock(&srv->lock);
        return PAL_ERROR_NO_MEMORY;
    }
    mac_uri_entry_t *e = &srv->entries[srv->entry_count++];
    strncpy(e->uri, uri, sizeof(e->uri) - 1);
    e->method         = method;
    e->handler        = handler;
    e->upload_handler = nullptr;
    e->user_ctx       = user_ctx;
    pthread_mutex_unlock(&srv->lock);

    LOG_MSG_DEBUG(HTTP_SRV_LOG_EN, "Registered handler: %s %s", uri,
                  method == PAL_HTTP_POST ? "POST" : "GET");
    return PAL_OK;
}

int32_t pal_http_server_register_uri_with_upload(
    pal_http_server_handle_t server_handle,
    const char *uri,
    pal_http_uri_handler_t handler,
    pal_http_upload_handler_t upload_handler,
    void *user_ctx)
{
    if (!server_handle || !uri || !upload_handler) return PAL_ERROR_INVALID;
    mac_http_server_t *srv = (mac_http_server_t *)server_handle;

    pthread_mutex_lock(&srv->lock);
    if (srv->entry_count >= MAC_MAX_HANDLERS) {
        pthread_mutex_unlock(&srv->lock);
        return PAL_ERROR_NO_MEMORY;
    }
    mac_uri_entry_t *e = &srv->entries[srv->entry_count++];
    strncpy(e->uri, uri, sizeof(e->uri) - 1);
    e->method         = PAL_HTTP_POST;
    e->handler        = handler;        /* completion handler — called after upload */
    e->upload_handler = upload_handler;
    e->user_ctx       = user_ctx;
    pthread_mutex_unlock(&srv->lock);

    LOG_MSG_DEBUG(HTTP_SRV_LOG_EN, "Registered upload handler: POST %s", uri);
    return PAL_OK;
}

/*===========================================================================*/
/*                      REQUEST OPERATIONS                                   */
/*===========================================================================*/

size_t pal_http_req_get_content_len(pal_http_request_t req)
{
    if (!req) return 0;
    return (size_t)((mac_req_ctx_t *)req)->content_length;
}

int32_t pal_http_req_recv(pal_http_request_t req, char *buf,
                           size_t buf_len, size_t *received)
{
    if (!req || !buf) return PAL_ERROR_INVALID;
    mac_req_ctx_t *r = (mac_req_ctx_t *)req;

    int ret = (int)recv(r->client_fd, buf, buf_len, 0);
    if (ret < 0) return PAL_ERROR;
    if (received) *received = (size_t)ret;
    return PAL_OK;
}

int32_t pal_http_req_get_header(pal_http_request_t req, const char *field,
                                 char *val, size_t val_size)
{
    if (!req || !field || !val) return PAL_ERROR_INVALID;
    const char *v = _get_header((mac_req_ctx_t *)req, field);
    if (!v) return PAL_ERROR_NOT_FOUND;
    strncpy(val, v, val_size - 1);
    val[val_size - 1] = '\0';
    return PAL_OK;
}

int32_t pal_http_req_get_query_param(pal_http_request_t req, const char *key,
                                      char *val, size_t val_size)
{
    if (!req || !key || !val) return PAL_ERROR_INVALID;
    mac_req_ctx_t *r = (mac_req_ctx_t *)req;

    /* Search in query string: key=value&... */
    const char *p = r->query;
    size_t key_len = strlen(key);
    while (*p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *v = p + key_len + 1;
            size_t v_len = strcspn(v, "&");
            strncpy(val, v, v_len < val_size - 1 ? v_len : val_size - 1);
            val[v_len < val_size - 1 ? v_len : val_size - 1] = '\0';
            return PAL_OK;
        }
        const char *amp = strchr(p, '&');
        p = amp ? amp + 1 : p + strlen(p);
    }
    return PAL_ERROR_NOT_FOUND;
}

/*===========================================================================*/
/*                     RESPONSE OPERATIONS                                   */
/*===========================================================================*/

int32_t pal_http_resp_set_status(pal_http_request_t req, int status_code)
{
    if (!req) return PAL_ERROR_INVALID;
    ((mac_req_ctx_t *)req)->resp_status_code = status_code;
    return PAL_OK;
}

int32_t pal_http_resp_set_type(pal_http_request_t req, const char *content_type)
{
    if (!req || !content_type) return PAL_ERROR_INVALID;
    mac_req_ctx_t *r = (mac_req_ctx_t *)req;
    strncpy(r->resp_content_type, content_type,
            sizeof(r->resp_content_type) - 1);
    return PAL_OK;
}

int32_t pal_http_resp_set_header(pal_http_request_t req,
                                  const char * /*field*/,
                                  const char * /*value*/)
{
    if (!req) return PAL_ERROR_INVALID;
    /* HTTP/1.0 mode: extra headers are not supported in this PAL implementation.
     * Headers are written atomically in pal_http_resp_send(). */
    return PAL_OK;
}

int32_t pal_http_resp_send(pal_http_request_t req, const char *data, size_t len)
{
    if (!req) return PAL_ERROR_INVALID;
    mac_req_ctx_t *r = (mac_req_ctx_t *)req;

    size_t body_len = (len == 0 && data) ? strlen(data) : len;

    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        r->resp_status_code,
        _status_reason(r->resp_status_code),
        r->resp_content_type,
        body_len);

    send(r->client_fd, header, (size_t)hlen, 0);
    if (data && body_len > 0) {
        send(r->client_fd, data, body_len, 0);
    }
    r->header_sent = true;
    return PAL_OK;
}

int32_t pal_http_resp_send_chunk(pal_http_request_t req,
                                  const char *data, size_t len)
{
    if (!req) return PAL_ERROR_INVALID;
    mac_req_ctx_t *r = (mac_req_ctx_t *)req;

    if (!r->header_sent) {
        /* Send streaming header — no Content-Length (HTTP/1.0 close signals end) */
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.0 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Connection: close\r\n"
            "\r\n",
            r->resp_status_code,
            _status_reason(r->resp_status_code),
            r->resp_content_type);
        send(r->client_fd, header, (size_t)hlen, 0);
        r->header_sent = true;
    }

    if (data && len > 0) {
        send(r->client_fd, data, len, 0);
    }
    return PAL_OK;
}

int32_t pal_http_resp_send_file(pal_http_request_t req, const char *filepath,
                                 const char *content_type)
{
    if (!req || !filepath) return PAL_ERROR_INVALID;

    const char *mime = content_type ? content_type : _get_mime_type(filepath);
    pal_http_resp_set_type(req, mime);

    /* Read from SPIFFS into a 4 KB scratch buffer and stream to client */
    static constexpr size_t k_chunk = 4096;
    uint8_t *buf = (uint8_t *)malloc(k_chunk);
    if (!buf) return PAL_ERROR_NO_MEMORY;

    /* For simplicity, read the full file (SPIFFS PAL has no seek) */
    size_t bytes_read = 0;
    int32_t ret = pal_spiffs_file_read(filepath, buf, k_chunk, &bytes_read);
    if (ret != PAL_OK || bytes_read == 0) {
        free(buf);
        return pal_http_resp_send_404(req);
    }

    /* If file fits in one chunk, send with Content-Length */
    if (bytes_read < k_chunk) {
        mac_req_ctx_t *r = (mac_req_ctx_t *)req;
        char header[256];
        int hlen = snprintf(header, sizeof(header),
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n",
            r->resp_content_type, bytes_read);
        send(r->client_fd, header, (size_t)hlen, 0);
        send(r->client_fd, buf, bytes_read, 0);
    } else {
        /* Larger file: stream without Content-Length */
        pal_http_resp_send_chunk(req, (const char *)buf, bytes_read);
        /* TODO: loop for larger SPIFFS files (pal_spiffs does not support seek) */
        pal_http_resp_send_chunk(req, nullptr, 0);
    }

    free(buf);
    return PAL_OK;
}

int32_t pal_http_resp_send_404(pal_http_request_t req)
{
    if (!req) return PAL_ERROR_INVALID;
    pal_http_resp_set_status(req, 404);
    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req, "{\"error\":\"not found\"}", 0);
}

int32_t pal_http_resp_send_500(pal_http_request_t req)
{
    if (!req) return PAL_ERROR_INVALID;
    pal_http_resp_set_status(req, 500);
    pal_http_resp_set_type(req, "application/json");
    return pal_http_resp_send(req, "{\"error\":\"internal server error\"}", 0);
}

/*===========================================================================*/
/*                  OTA PAL STUBS (not used by ModuleWebServer)              */
/*                                                                           */
/*  ModuleWebServer uses the HSYS OTA message protocol, not these functions. */
/*  Provide stubs so pal_http_server.h compiles on mac.                      */
/*===========================================================================*/

int32_t pal_ota_begin(pal_ota_handle_t *ota_handle)
{
    (void)ota_handle;
    return PAL_ERROR;
}

int32_t pal_ota_write(pal_ota_handle_t ota_handle,
                      const uint8_t *data, size_t len)
{
    (void)ota_handle; (void)data; (void)len;
    return PAL_ERROR;
}

int32_t pal_ota_end(pal_ota_handle_t ota_handle)
{
    (void)ota_handle;
    return PAL_ERROR;
}

int32_t pal_ota_abort(pal_ota_handle_t ota_handle)
{
    (void)ota_handle;
    return PAL_ERROR;
}

const char *pal_ota_get_status_string(void)
{
    return "not-used";
}
