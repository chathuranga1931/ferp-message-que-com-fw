// pal_mac_http_client.cpp
//
// Simulator implementation of pal_http_client.h using macOS libcurl.
//
// The cube_sphere_api calls these functions to perform HTTPS requests.
// curl is universally available on macOS and supports TLS, custom headers,
// and response body buffering — everything the CubeSphere driver needs.
//
// Link against: -lcurl  (add curl to simulator CMakeLists target_link_libraries)

#include "pal_http_client.h"
#include "pal_logger.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#define __TAG__    "PAL_HTTP"
#define HTTP_LOG   true

// ---------------------------------------------------------------------------
// Internal context
// ---------------------------------------------------------------------------

#define MAX_COLLECT_HEADERS  4
#define MAX_RESPONSE_HEADERS 16

typedef struct {
    char *url;
    char *cert_pem;          // path to PEM file, or NULL
    uint32_t timeout_ms;

    // Request headers set by the caller
    struct curl_slist *req_headers;

    // Header keys the caller wants to capture from the response
    char collect_keys[MAX_COLLECT_HEADERS][PAL_HTTP_MAX_HEADER_LEN];
    uint8_t collect_count;

    // Captured response headers
    char captured_keys[MAX_RESPONSE_HEADERS][PAL_HTTP_MAX_HEADER_LEN];
    char captured_vals[MAX_RESPONSE_HEADERS][PAL_HTTP_MAX_HEADER_LEN];
    uint8_t captured_count;

    // Temporary PEM cert file (written when cert_pem is PEM content, not a path)
    char _tmp_cert_path[64];
    int  _tmp_cert_fd;
} pal_http_ctx_t;

// ---------------------------------------------------------------------------
// Response body accumulator
// ---------------------------------------------------------------------------

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} body_buf_t;

static size_t _write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    body_buf_t *b = (body_buf_t *)userdata;
    size_t bytes = size * nmemb;
    size_t needed = b->len + bytes + 1;
    if (needed > b->cap) {
        size_t new_cap = needed * 2;
        char *tmp = (char *)realloc(b->buf, new_cap);
        if (!tmp) return 0;
        b->buf = tmp;
        b->cap = new_cap;
    }
    memcpy(b->buf + b->len, ptr, bytes);
    b->len += bytes;
    b->buf[b->len] = '\0';
    return bytes;
}

// ---------------------------------------------------------------------------
// Response header capture
// ---------------------------------------------------------------------------

static size_t _header_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    pal_http_ctx_t *ctx = (pal_http_ctx_t *)userdata;
    size_t bytes = size * nmemb;

    // Headers arrive as "Key: Value\r\n" lines
    char line[512];
    size_t copy_len = bytes < sizeof(line) - 1 ? bytes : sizeof(line) - 1;
    memcpy(line, ptr, copy_len);
    line[copy_len] = '\0';

    // Strip trailing \r\n
    char *end = line + strlen(line);
    while (end > line && (end[-1] == '\r' || end[-1] == '\n')) end--;
    *end = '\0';

    // Find colon separator
    char *colon = strchr(line, ':');
    if (!colon) return bytes;

    char key[PAL_HTTP_MAX_HEADER_LEN] = {};
    char val[PAL_HTTP_MAX_HEADER_LEN] = {};
    size_t key_len = (size_t)(colon - line);
    if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
    memcpy(key, line, key_len);

    const char *v = colon + 1;
    while (*v == ' ') v++;
    strncpy(val, v, sizeof(val) - 1);

    // Check if caller wants this header
    for (uint8_t i = 0; i < ctx->collect_count; i++) {
        if (strcasecmp(key, ctx->collect_keys[i]) == 0) {
            if (ctx->captured_count < MAX_RESPONSE_HEADERS) {
                strncpy(ctx->captured_keys[ctx->captured_count], key,
                        sizeof(ctx->captured_keys[0]) - 1);
                strncpy(ctx->captured_vals[ctx->captured_count], val,
                        sizeof(ctx->captured_vals[0]) - 1);
                ctx->captured_count++;
            }
            break;
        }
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static CURL *_make_curl(pal_http_ctx_t *ctx, body_buf_t *body)
{
    CURL *curl = curl_easy_init();
    if (!curl) return nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, ctx->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, _header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)(ctx->timeout_ms ? ctx->timeout_ms : 10000));

    if (ctx->cert_pem && strncmp(ctx->cert_pem, "-----BEGIN", 10) == 0) {
        // PEM certificate content (as used by ESP-IDF cert_pem field).
        // curl needs a file path, so write to a temp file for this request.
        ctx->_tmp_cert_fd = -1;
        snprintf(ctx->_tmp_cert_path, sizeof(ctx->_tmp_cert_path),
                 "/tmp/pal_ca_XXXXXX.pem");
        ctx->_tmp_cert_fd = mkstemps(ctx->_tmp_cert_path, 4);
        if (ctx->_tmp_cert_fd >= 0) {
            write(ctx->_tmp_cert_fd, ctx->cert_pem, strlen(ctx->cert_pem));
            close(ctx->_tmp_cert_fd);
            curl_easy_setopt(curl, CURLOPT_CAINFO, ctx->_tmp_cert_path);
        } else {
            LOG_MSG_WARNING(HTTP_LOG, "could not write temp CA file — using system CAs");
        }
    } else if (ctx->cert_pem) {
        // Treat as file path only if it actually exists on disk
        struct stat st;
        if (stat(ctx->cert_pem, &st) == 0) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, ctx->cert_pem);
        } else {
            // Not a PEM cert and not a valid file path (e.g. an API secret was
            // passed here by mistake) — fall back to system CA store.
            LOG_MSG_WARNING(HTTP_LOG,
                "cert_pem is not a PEM cert or valid path — using system CAs");
        }
    }
    // NULL cert_pem → use macOS system CA store (peer verification stays ON)

    if (ctx->req_headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, ctx->req_headers);
    }

    return curl;
}

static int32_t _finish(CURL *curl, body_buf_t *body, pal_http_response_t *response)
{
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (response) {
        response->body     = body->buf;  // caller owns this memory
        response->body_len = body->len;
        response->status_code = (int32_t)http_code;
    } else {
        free(body->buf);
    }

    return (int32_t)http_code;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int32_t pal_http_client_init(const pal_http_client_config_t *config,
                              pal_http_client_handle_t *handle)
{
    if (!config || !handle || !config->url) return -1;

    pal_http_ctx_t *ctx = (pal_http_ctx_t *)calloc(1, sizeof(pal_http_ctx_t));
    if (!ctx) return -1;

    ctx->url = strdup(config->url);
    if (config->cert_pem) ctx->cert_pem = strdup(config->cert_pem);
    ctx->timeout_ms = config->timeout_ms;
    ctx->_tmp_cert_fd = -1;
    ctx->_tmp_cert_path[0] = '\0';

    *handle = ctx;
    return 0;
}

int32_t pal_http_client_set_url(pal_http_client_handle_t handle, const char *url)
{
    pal_http_ctx_t *ctx = (pal_http_ctx_t *)handle;
    if (!ctx || !url) return -1;
    free(ctx->url);
    ctx->url = strdup(url);
    return 0;
}

int32_t pal_http_client_set_header(pal_http_client_handle_t handle,
                                    const char *key, const char *value)
{
    pal_http_ctx_t *ctx = (pal_http_ctx_t *)handle;
    if (!ctx || !key || !value) return -1;

    char header[PAL_HTTP_MAX_HEADER_LEN * 2];
    snprintf(header, sizeof(header), "%s: %s", key, value);
    ctx->req_headers = curl_slist_append(ctx->req_headers, header);
    return ctx->req_headers ? 0 : -1;
}

int32_t pal_http_client_collect_headers(pal_http_client_handle_t handle,
                                         const char **header_keys, uint8_t count)
{
    pal_http_ctx_t *ctx = (pal_http_ctx_t *)handle;
    if (!ctx || !header_keys) return -1;

    if (count > MAX_COLLECT_HEADERS) count = MAX_COLLECT_HEADERS;
    ctx->collect_count = 0;
    for (uint8_t i = 0; i < count; i++) {
        strncpy(ctx->collect_keys[i], header_keys[i], PAL_HTTP_MAX_HEADER_LEN - 1);
        ctx->collect_count++;
    }
    return 0;
}

int32_t pal_http_client_get(pal_http_client_handle_t handle,
                             pal_http_response_t *response)
{
    pal_http_ctx_t *ctx = (pal_http_ctx_t *)handle;
    if (!ctx) return -1;

    // Reset captured headers for this request
    ctx->captured_count = 0;

    body_buf_t body = {};
    CURL *curl = _make_curl(ctx, &body);
    if (!curl) return -1;

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_MSG_ERROR(HTTP_LOG, "curl GET failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(body.buf);
        if (response) { response->body = nullptr; response->body_len = 0; }
        return -1;
    }

    return _finish(curl, &body, response);
}

int32_t pal_http_client_post(pal_http_client_handle_t handle,
                              const char *body_data, size_t body_len,
                              pal_http_response_t *response)
{
    pal_http_ctx_t *ctx = (pal_http_ctx_t *)handle;
    if (!ctx) return -1;

    ctx->captured_count = 0;

    body_buf_t resp_body = {};
    CURL *curl = _make_curl(ctx, &resp_body);
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        LOG_MSG_ERROR(HTTP_LOG, "curl POST failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(resp_body.buf);
        if (response) { response->body = nullptr; response->body_len = 0; }
        return -1;
    }

    return _finish(curl, &resp_body, response);
}

int32_t pal_http_client_get_header(pal_http_client_handle_t handle,
                                    const char *key, char *value, size_t value_len)
{
    pal_http_ctx_t *ctx = (pal_http_ctx_t *)handle;
    if (!ctx || !key || !value) return -1;

    for (uint8_t i = 0; i < ctx->captured_count; i++) {
        if (strcasecmp(ctx->captured_keys[i], key) == 0) {
            strncpy(value, ctx->captured_vals[i], value_len - 1);
            value[value_len - 1] = '\0';
            return 0;
        }
    }
    return -1;  // not found
}

int32_t pal_http_client_get_stream(pal_http_client_handle_t handle,
                                    pal_http_stream_chunk_cb_t chunk_cb,
                                    void *user_ctx)
{
    // Not needed by cube_sphere — stub that falls back to buffered GET
    (void)chunk_cb; (void)user_ctx;
    pal_http_response_t resp = {};
    int32_t ret = pal_http_client_get(handle, &resp);
    pal_http_response_free(&resp);
    return ret;
}

int32_t pal_http_client_get_response_body(pal_http_client_handle_t handle,
                                           char *buffer, size_t buffer_len)
{
    (void)handle; (void)buffer; (void)buffer_len;
    return -1;  // not used by cube_sphere
}

void pal_http_response_free(pal_http_response_t *response)
{
    if (!response) return;
    free(response->body);
    response->body     = nullptr;
    response->body_len = 0;
}

int32_t pal_http_client_cleanup(pal_http_client_handle_t handle)
{
    pal_http_ctx_t *ctx = (pal_http_ctx_t *)handle;
    if (!ctx) return 0;
    free(ctx->url);
    free(ctx->cert_pem);
    if (ctx->req_headers) curl_slist_free_all(ctx->req_headers);
    // Remove temp CA cert file if one was created
    if (ctx->_tmp_cert_fd >= 0 && ctx->_tmp_cert_path[0]) {
        unlink(ctx->_tmp_cert_path);
    }
    free(ctx);
    return 0;
}
