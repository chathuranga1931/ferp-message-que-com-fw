// module_http.cpp
//
// ModuleHttp — centralised message-based HTTP/HTTPS client.
//
// Lifecycle:
//   init()             — subscribe to MSG_ID_HTTP_REQUEST
//   on_msg_received()  — IDLE / EXECUTING state handling
//   _execute()         — blocks the task; calls PAL HTTP; sends response headers + result

#include "module_http.h"

#include <cstdlib>   // free()

// Messages
#include "msg_http_request.h"
#include "msg_http_result.h"
#include "msg_http_response_header.h"

#include "pal_logger.h"
#include "pal_http_client.h"

#include <string.h>

#define __TAG__ "MOD_HTTP"
#ifndef MOD_HTTP_LOG_EN
#define MOD_HTTP_LOG_EN  true
#endif

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleHttp s_instance;
ModuleHttp *ModuleHttp::instance() { return &s_instance; }

// ── Lifecycle ──────────────────────────────────────────────────────────────────

void ModuleHttp::init()
{
    subscribe(MSG_ID_HTTP_REQUEST);
    LOG_MSG_INFO(MOD_HTTP_LOG_EN, "init: ready");
}

// ── Message dispatch ───────────────────────────────────────────────────────────

void ModuleHttp::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MSG_ID_HTTP_REQUEST:
            _handle_http_request(msg);
            break;
        default:
            break;
    }
}

// ── Handler ───────────────────────────────────────────────────────────────────

void ModuleHttp::_handle_http_request(const hsys_msg_t &msg)
{
    // If busy, reject immediately with HTTP_RESULT_BUSY
    if (_state != IDLE) {
        LOG_MSG_WARNING(MOD_HTTP_LOG_EN,
                        "_handle_http_request: BUSY (owner=%u) — rejecting sender=%u",
                        (unsigned)_owner_id, (unsigned)msg.sender_id);
        hsys_msg_t *result = MsgHttpResult::create(id(), HTTP_RESULT_BUSY, 0, nullptr, 0U);
        if (result) send(result, msg.sender_id);
        return;
    }

    auto f = MsgHttpRequest::parse(msg);

    _owner_id      = msg.sender_id;
    _method        = f.method;
    _timeout_ms    = f.timeout_ms;
    _cert_pem      = f.rootca;

    // Copy URL (add NUL terminator)
    if (f.url && f.url_len > 0U) {
        uint16_t copy = (f.url_len < MODULE_HTTP_MAX_URL_LEN - 1U)
                        ? f.url_len : (uint16_t)(MODULE_HTTP_MAX_URL_LEN - 1U);
        memcpy(_url, f.url, copy);
        _url[copy] = '\0';
    } else {
        _url[0] = '\0';
    }

    // Unpack headers: key\0value\0key\0value\0…
    _hdr_count = 0U;
    if (f.headers_buf && f.headers_len > 0U) {
        const char *p   = reinterpret_cast<const char *>(f.headers_buf);
        const char *end = p + f.headers_len;
        while (p < end && _hdr_count < PAL_HTTP_MAX_HEADERS) {
            const char *key = p;
            size_t klen = strnlen(key, (size_t)(end - p));
            if (klen == 0U || p + klen >= end) break;
            p += klen + 1U;  // skip NUL
            const char *val = p;
            size_t vlen = strnlen(val, (size_t)(end - p));
            p += vlen + 1U;  // skip NUL

            strncpy(_hdr_keys[_hdr_count], key, MODULE_HTTP_MAX_HEADER_KEY - 1U);
            _hdr_keys[_hdr_count][MODULE_HTTP_MAX_HEADER_KEY - 1U] = '\0';
            strncpy(_hdr_vals[_hdr_count], val, MODULE_HTTP_MAX_HEADER_VAL - 1U);
            _hdr_vals[_hdr_count][MODULE_HTTP_MAX_HEADER_VAL - 1U] = '\0';
            _hdr_count++;
        }
    }

    // Copy body
    _body_len = 0U;
    if (f.body && f.body_len > 0U) {
        uint16_t copy = (f.body_len <= MODULE_HTTP_MAX_REQUEST_BODY)
                        ? f.body_len : (uint16_t)MODULE_HTTP_MAX_REQUEST_BODY;
        memcpy(_body_buf, f.body, copy);
        _body_len = copy;
    }

    // Response header collection
    _collect_count = 0U;
    if (f.collect_key && f.collect_key[0] != '\0' && f.collect_count > 0U) {
        strncpy(_collect_keys[0], f.collect_key, MODULE_HTTP_MAX_HEADER_KEY - 1U);
        _collect_keys[0][MODULE_HTTP_MAX_HEADER_KEY - 1U] = '\0';
        _collect_count = 1U;
    }

    // Stream sink
    _has_stream_sink       = (f.stream_sink != nullptr);
    _stream_drv            = static_cast<const ota_fs_driver_t *>(f.stream_sink);
    _stream_ctx            = f.stream_ctx;
    _stream_crc32_expected = f.stream_crc32;

    _state = EXECUTING;

    LOG_MSG_INFO(MOD_HTTP_LOG_EN,
                 "_handle_http_request: executing  url='%.80s'  method=%d  hdrs=%u  body=%u  stream=%d",
                 _url, (int)_method, (unsigned)_hdr_count, (unsigned)_body_len,
                 (int)_has_stream_sink);

    _execute();
    _reset_session();
}

// ── Execution ─────────────────────────────────────────────────────────────────

void ModuleHttp::_execute()
{
    pal_http_client_handle_t handle = nullptr;

    // Build PAL config
    pal_http_client_config_t cfg = {};
    cfg.url        = _url;
    cfg.cert_pem   = _cert_pem;
    cfg.timeout_ms = (_timeout_ms > 0U) ? _timeout_ms : 30000U;
    cfg.keep_alive = false;

    int32_t rc = pal_http_client_init(&cfg, &handle);
    if (rc != 0 || !handle) {
        LOG_MSG_ERROR(MOD_HTTP_LOG_EN, "_execute: pal_http_client_init failed (%d)", (int)rc);
        _send_result(HTTP_RESULT_CONNECT_FAILED, 0, nullptr, 0U);
        return;
    }

    // Register response headers to capture (if requested by session owner)
    if (_collect_count > 0U) {
        const char *ckeys[MSG_HTTP_MAX_COLLECT_KEYS];
        for (uint8_t i = 0U; i < _collect_count; ++i) ckeys[i] = _collect_keys[i];
        pal_http_client_collect_headers(handle, ckeys, _collect_count);
    }

    // Apply request headers
    for (uint8_t i = 0U; i < _hdr_count; ++i) {
        pal_http_client_set_header(handle, _hdr_keys[i], _hdr_vals[i]);
    }

    // Execute HTTP call — streaming or buffered
    pal_http_response_t resp = {};
    if (_has_stream_sink) {
        // Streaming path: deliver binary body directly into the ota_fs_driver_t
        struct _StreamCtx {
            const ota_fs_driver_t *drv;
            void                  *ctx;
            uint32_t               crc32;
            uint32_t               bytes;
            bool                   fopen_done;
            bool                   error;
        };
        _StreamCtx sctx{};
        sctx.drv = _stream_drv;
        sctx.ctx = _stream_ctx;
        sctx.crc32 = 0U;

        auto chunk_cb = [](const uint8_t *data, size_t len, void *user) -> int32_t {
            auto *s = static_cast<_StreamCtx *>(user);
            if (s->error || len == 0U) return s->error ? -1 : 0;
            if (!s->fopen_done) {
                if (s->drv->fopen(s->ctx, nullptr, OTA_FS_OPEN_WRITE) != OTA_FS_OK) {
                    s->error = true; return -1;
                }
                s->fopen_done = true;
            }
            if (s->drv->fwrite(s->ctx, data, (uint32_t)len) != OTA_FS_OK) {
                s->error = true; return -1;
            }
            s->crc32   = crc32_update(s->crc32, data, len);
            s->bytes  += (uint32_t)len;
            return 0;
        };

        rc = pal_http_client_get_stream(handle, chunk_cb, &sctx);
        pal_http_client_cleanup(handle);
        handle = nullptr;

        // Determine result
        http_result_t stream_result;
        if (rc < 0) {
            stream_result = HTTP_RESULT_ERROR;
        } else if (rc != 200) {
            stream_result = HTTP_RESULT_ERROR;
        } else if (sctx.error || !sctx.fopen_done || sctx.bytes == 0U) {
            stream_result = HTTP_RESULT_ERROR;
        } else if (_stream_crc32_expected != 0U && sctx.crc32 != _stream_crc32_expected) {
            LOG_MSG_ERROR(MOD_HTTP_LOG_EN,
                          "_execute(stream): CRC mismatch  got=0x%08lX  expected=0x%08lX",
                          (unsigned long)sctx.crc32, (unsigned long)_stream_crc32_expected);
            stream_result = HTTP_RESULT_ERROR;
        } else {
            stream_result = HTTP_RESULT_SUCCESS;
        }

        if (stream_result == HTTP_RESULT_SUCCESS) {
            if (_stream_drv->fclose(_stream_ctx) != OTA_FS_OK) {
                LOG_MSG_ERROR(MOD_HTTP_LOG_EN, "_execute(stream): fclose failed");
                stream_result = HTTP_RESULT_ERROR;
                _stream_drv->ferase(_stream_ctx);
            } else {
                LOG_MSG_INFO(MOD_HTTP_LOG_EN,
                             "_execute(stream): %lu bytes  CRC=0x%08lX",
                             (unsigned long)sctx.bytes, (unsigned long)sctx.crc32);
            }
        } else {
            if (sctx.fopen_done) _stream_drv->ferase(_stream_ctx);
        }

        _send_result(stream_result, (stream_result == HTTP_RESULT_SUCCESS) ? 200 : rc,
                     nullptr, 0U);
        return;
    }

    if (_method == PAL_HTTP_METHOD_GET || _method == PAL_HTTP_METHOD_HEAD) {
        rc = pal_http_client_get(handle, &resp);
    } else {
        rc = pal_http_client_post(handle,
                                   (const char *)_body_buf, _body_len,
                                   &resp);
    }

    pal_http_client_cleanup(handle);
    handle = nullptr;

    // Determine result code
    http_result_t result;
    if (rc < 0) {
        // Map PAL transport error codes (pal_http_client.h) to http_result_t
        if (rc == PAL_HTTP_ERR_TLS) {
            result = HTTP_RESULT_TLS_FAILED;
        } else if (rc == PAL_HTTP_ERR_TIMEOUT) {
            result = HTTP_RESULT_TIMEOUT;
        } else if (rc == PAL_HTTP_ERR_CONNECT) {
            result = HTTP_RESULT_CONNECT_FAILED;
        } else {
            result = HTTP_RESULT_ERROR;
        }
        LOG_MSG_WARNING(MOD_HTTP_LOG_EN,
                        "_execute: PAL returned %d → result=%d", (int)rc, (int)result);
        _send_result(result, 0, nullptr, 0U);
        return;
    }

    int32_t status_code = resp.status_code;

    // Send one MsgHttpResponseHeader per collected response header
    for (uint8_t i = 0U; i < resp.header_count; ++i) {
        if (!resp.headers[i]) continue;
        // PAL provides headers as "Key: Value" strings — split on ": "
        char  hdr_copy[MODULE_HTTP_MAX_HEADER_KEY + MODULE_HTTP_MAX_HEADER_VAL + 4U] = {};
        strncpy(hdr_copy, resp.headers[i], sizeof(hdr_copy) - 1U);
        char *sep = strstr(hdr_copy, ": ");
        if (sep) {
            *sep = '\0';
            _send_response_header(hdr_copy, sep + 2U);
        }
    }

    // Build result message
    size_t body_len = resp.body_len;
    if (body_len > MODULE_HTTP_MAX_RESPONSE_BODY) {
        body_len = MODULE_HTTP_MAX_RESPONSE_BODY;
        result   = HTTP_RESULT_BODY_TOO_LARGE;
    } else {
        result = HTTP_RESULT_SUCCESS;
    }

    _send_result(result, status_code, resp.body, (uint32_t)body_len);

    // Free PAL-allocated body (PAL contract: caller frees resp.body)
    if (resp.body) {
        void *b = resp.body;
        resp.body = nullptr;
        free(b);
    }
    for (uint8_t i = 0U; i < resp.header_count; ++i) {
        if (resp.headers[i]) {
            void *h = resp.headers[i];
            resp.headers[i] = nullptr;
            free(h);
        }
    }
}

// ── Send helpers ───────────────────────────────────────────────────────────────

void ModuleHttp::_send_result(http_result_t result, int32_t status_code,
                               const void *body, uint32_t body_len)
{
    if (_owner_id == HSYS_MODULE_ID_INVALID) return;

    hsys_msg_t *msg = MsgHttpResult::create(id(), result, status_code, body, body_len);
    if (msg) {
        send(msg, _owner_id);
        LOG_MSG_INFO(MOD_HTTP_LOG_EN,
                     "_send_result: result=%d  status=%d  body=%u bytes → owner=%u",
                     (int)result, (int)status_code, (unsigned)body_len, (unsigned)_owner_id);
    } else {
        LOG_MSG_ERROR(MOD_HTTP_LOG_EN, "_send_result: pool full");
    }
}

void ModuleHttp::_send_response_header(const char *key, const char *value)
{
    if (_owner_id == HSYS_MODULE_ID_INVALID) return;
    hsys_msg_t *msg = MsgHttpResponseHeader::create(id(), key, value);
    if (msg) send(msg, _owner_id);
}

// ── Utility ────────────────────────────────────────────────────────────────────

void ModuleHttp::_reset_session()
{
    _state         = IDLE;
    _owner_id      = HSYS_MODULE_ID_INVALID;
    _method        = PAL_HTTP_METHOD_GET;
    _timeout_ms    = 0U;
    _cert_pem      = nullptr;
    _url[0]        = '\0';
    _hdr_count     = 0U;
    _body_len      = 0U;
    _collect_count = 0U;
    _stream_drv    = nullptr;
    _stream_ctx    = nullptr;
    _stream_crc32_expected = 0U;
    _has_stream_sink = false;
    memset(_hdr_keys,     0, sizeof(_hdr_keys));
    memset(_hdr_vals,     0, sizeof(_hdr_vals));
    memset(_collect_keys, 0, sizeof(_collect_keys));
}
