// module_http.cpp
//
// ModuleHttp — centralised message-based HTTP/HTTPS client.
//
// Lifecycle:
//   pre_init()         — create the one-shot idle timer (not started yet)
//   init()             — subscribe to all incoming HTTP message IDs
//   on_msg_received()  — IDLE / SESSION_OPEN state handling
//   on_wake()          — idle timer expiry (fired from timer daemon thread)
//   _execute()         — blocks the task; calls PAL HTTP; sends response headers + result

#include "module_http.h"

#include <cstdlib>   // free()

// Messages
#include "msg_http_start_request.h"
#include "msg_http_start_response.h"
#include "msg_http_set_url_request.h"
#include "msg_http_set_url_response.h"
#include "msg_http_set_root_ca_request.h"
#include "msg_http_set_root_ca_response.h"
#include "msg_http_header_request.h"
#include "msg_http_header_response.h"
#include "msg_http_body_request.h"
#include "msg_http_body_response.h"
#include "msg_http_send_request.h"
#include "msg_http_abort_request.h"
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

void ModuleHttp::pre_init()
{
    memset(_url,      0, sizeof(_url));
    memset(_hdr_keys, 0, sizeof(_hdr_keys));
    memset(_hdr_vals, 0, sizeof(_hdr_vals));
    memset(_body_buf, 0, sizeof(_body_buf));

    _idle_timer = hsys_timer_create(
        "http_idle",
        MODULE_HTTP_IDLE_TIMEOUT_MS,
        /*auto_reload=*/false,
        /*user_data=*/this,
        _idle_timer_cb);

    if (!_idle_timer) {
        LOG_MSG_ERROR(MOD_HTTP_LOG_EN, "pre_init: failed to create idle timer");
    }
}

void ModuleHttp::init()
{
    subscribe(MSG_ID_HTTP_START_REQUEST);
    subscribe(MSG_ID_HTTP_START_RESPONSE);
    subscribe(MSG_ID_HTTP_SET_URL_REQUEST);
    subscribe(MSG_ID_HTTP_SET_URL_RESPONSE);
    subscribe(MSG_ID_HTTP_SET_ROOT_CA_REQ);
    subscribe(MSG_ID_HTTP_SET_ROOT_CA_RESP);
    subscribe(MSG_ID_HTTP_HEADER_REQUEST);
    subscribe(MSG_ID_HTTP_HEADER_RESPONSE);
    subscribe(MSG_ID_HTTP_BODY_REQUEST);
    subscribe(MSG_ID_HTTP_BODY_RESPONSE);
    subscribe(MSG_ID_HTTP_SEND_REQUEST);
    subscribe(MSG_ID_HTTP_RESULT);
    subscribe(MSG_ID_HTTP_ABORT_REQUEST);
    subscribe(MSG_ID_HTTP_RESPONSE_HEADER);

    LOG_MSG_INFO(MOD_HTTP_LOG_EN, "init: ready  (idle timeout %u ms)",
                 (unsigned)MODULE_HTTP_IDLE_TIMEOUT_MS);
}

// ── Idle timer ─────────────────────────────────────────────────────────────────

void ModuleHttp::_idle_timer_cb(void *timer_handle)
{
    void *ud = hsys_timer_get_userdata(timer_handle);
    static_cast<ModuleHttp *>(ud)->_on_idle_timer_fired();
}

void ModuleHttp::_on_idle_timer_fired()
{
    _idle_expired = true;
    wake();  // enqueue wake token → on_wake() runs on the module task
}

void ModuleHttp::_reset_idle_timer()
{
    if (_idle_timer) hsys_reset_timer(_idle_timer);
}

void ModuleHttp::_cancel_idle_timer()
{
    if (_idle_timer) hsys_stop_timer(_idle_timer);
}

// ── on_wake — idle timeout expiry ─────────────────────────────────────────────

void ModuleHttp::on_wake()
{
    if (!_idle_expired) return;
    _idle_expired = false;

    if (_state == SESSION_OPEN) {
        LOG_MSG_WARNING(MOD_HTTP_LOG_EN,
                        "on_wake: session expired for owner=%u",
                        (unsigned)_owner_id);
        _send_result(HTTP_RESULT_SESSION_EXPIRED, 0, nullptr, 0U);
        _reset_session();
    }
}

// ── Message dispatch ───────────────────────────────────────────────────────────

void ModuleHttp::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id)
    {
        case MSG_ID_HTTP_START_REQUEST:
            _handle_start_request(msg);
            break;

        case MSG_ID_HTTP_SET_URL_REQUEST:
            _handle_set_url(msg);
            break;

        case MSG_ID_HTTP_SET_ROOT_CA_REQ:
            _handle_set_root_ca(msg);
            break;

        case MSG_ID_HTTP_HEADER_REQUEST:
            _handle_header(msg);
            break;

        case MSG_ID_HTTP_BODY_REQUEST:
            _handle_body(msg);
            break;

        case MSG_ID_HTTP_SEND_REQUEST:
            _handle_send(msg);
            break;

        case MSG_ID_HTTP_ABORT_REQUEST:
            _handle_abort(msg);
            break;

        default:
            break;
    }
}

// ── Handlers ──────────────────────────────────────────────────────────────────

void ModuleHttp::_handle_start_request(const hsys_msg_t &msg)
{
    if (_state != IDLE) {
        LOG_MSG_INFO(MOD_HTTP_LOG_EN,
                     "StartRequest from %u: busy (owner=%u)",
                     (unsigned)msg.sender_id, (unsigned)_owner_id);
        _send_start_response(msg.sender_id, HTTP_SESSION_BUSY);
        return;
    }

    auto p = MsgHttpStartRequest::deserialize(msg);
    _owner_id      = msg.sender_id;
    _method        = p.method;
    _timeout_ms    = p.timeout_ms;
    _collect_count = (p.collect_count <= MSG_HTTP_MAX_COLLECT_KEYS)
                     ? p.collect_count : (uint8_t)MSG_HTTP_MAX_COLLECT_KEYS;
    memcpy(_collect_keys, p.collect_keys, sizeof(_collect_keys));
    _state         = SESSION_OPEN;

    _reset_idle_timer();
    hsys_start_timer(_idle_timer);

    LOG_MSG_INFO(MOD_HTTP_LOG_EN,
                 "StartRequest: session opened for owner=%u  method=%d  timeout=%u ms",
                 (unsigned)_owner_id, (int)_method, (unsigned)_timeout_ms);

    _send_start_response(_owner_id, HTTP_SESSION_OK);
}

void ModuleHttp::_handle_set_url(const hsys_msg_t &msg)
{
    if (!_is_owner(msg)) return;

    const char *url = MsgHttpSetUrlRequest::get_url(msg);
    if (!url) {
        _send_op_response(MSG_ID_HTTP_SET_URL_RESPONSE, _owner_id, HTTP_OP_ERR_BAD_STATE);
        return;
    }
    if (strnlen(url, MODULE_HTTP_MAX_URL_LEN) >= MODULE_HTTP_MAX_URL_LEN) {
        _send_op_response(MSG_ID_HTTP_SET_URL_RESPONSE, _owner_id, HTTP_OP_ERR_URL_TOO_LONG);
        return;
    }

    strncpy(_url, url, MODULE_HTTP_MAX_URL_LEN - 1U);
    _url[MODULE_HTTP_MAX_URL_LEN - 1U] = '\0';

    _reset_idle_timer();
    _send_op_response(MSG_ID_HTTP_SET_URL_RESPONSE, _owner_id, HTTP_OP_OK);
}

void ModuleHttp::_handle_set_root_ca(const hsys_msg_t &msg)
{
    if (!_is_owner(msg)) return;

    auto p  = MsgHttpSetRootCaRequest::deserialize(msg);
    _cert_pem = p.cert_pem;

    _reset_idle_timer();
    _send_op_response(MSG_ID_HTTP_SET_ROOT_CA_RESP, _owner_id, HTTP_OP_OK);
}

void ModuleHttp::_handle_header(const hsys_msg_t &msg)
{
    if (!_is_owner(msg)) return;

    if (_hdr_count >= PAL_HTTP_MAX_HEADERS) {
        _send_op_response(MSG_ID_HTTP_HEADER_RESPONSE, _owner_id, HTTP_OP_ERR_HEADERS_FULL);
        return;
    }

    const char *key = MsgHttpHeaderRequest::get_key(msg);
    const char *val = MsgHttpHeaderRequest::get_value(msg);

    if (!key || !val) {
        _send_op_response(MSG_ID_HTTP_HEADER_RESPONSE, _owner_id, HTTP_OP_ERR_BAD_STATE);
        return;
    }

    strncpy(_hdr_keys[_hdr_count], key, MODULE_HTTP_MAX_HEADER_KEY - 1U);
    _hdr_keys[_hdr_count][MODULE_HTTP_MAX_HEADER_KEY - 1U] = '\0';
    strncpy(_hdr_vals[_hdr_count], val, MODULE_HTTP_MAX_HEADER_VAL - 1U);
    _hdr_vals[_hdr_count][MODULE_HTTP_MAX_HEADER_VAL - 1U] = '\0';
    _hdr_count++;

    _reset_idle_timer();
    _send_op_response(MSG_ID_HTTP_HEADER_RESPONSE, _owner_id, HTTP_OP_OK);
}

void ModuleHttp::_handle_body(const hsys_msg_t &msg)
{
    if (!_is_owner(msg)) return;

    uint32_t len = 0U;
    const void *body = MsgHttpBodyRequest::get_body(msg, &len);

    if (len > MODULE_HTTP_MAX_REQUEST_BODY) {
        len = MODULE_HTTP_MAX_REQUEST_BODY;
    }
    _body_len = len;
    if (len > 0U && body) {
        memcpy(_body_buf, body, len);
    }

    _reset_idle_timer();
    _send_op_response(MSG_ID_HTTP_BODY_RESPONSE, _owner_id, HTTP_OP_OK);
}

void ModuleHttp::_handle_send(const hsys_msg_t &msg)
{
    if (!_is_owner(msg)) return;
    if (_state != SESSION_OPEN) return;

    _cancel_idle_timer();
    _state = EXECUTING;

    LOG_MSG_INFO(MOD_HTTP_LOG_EN,
                 "_handle_send: executing  url='%.80s'  method=%d  hdrs=%u  body=%u bytes",
                 _url, (int)_method, (unsigned)_hdr_count, (unsigned)_body_len);

    _execute();  // blocks until done

    _reset_session();
}

void ModuleHttp::_handle_abort(const hsys_msg_t &msg)
{
    if (!_is_owner(msg)) return;
    if (_state != SESSION_OPEN) return;

    LOG_MSG_INFO(MOD_HTTP_LOG_EN,
                 "_handle_abort: session aborted by owner=%u", (unsigned)_owner_id);

    _cancel_idle_timer();
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

    // Execute HTTP call
    pal_http_response_t resp = {};
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
        // PAL uses negative codes for transport/TLS errors
        if (rc == -0x4E || rc == -0x50) {
            result = HTTP_RESULT_TLS_FAILED;
        } else if (rc == -0x6800 || rc == -0x6900) {
            result = HTTP_RESULT_TIMEOUT;
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
        strncpy(hdr_copy, resp.headers[i],
                sizeof(hdr_copy) - 1U);
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
        // PAL allocates body with malloc — free via standard C
        // (pal_http_client.h documents this)
        // We use a void pointer cast to suppress C++ delete warnings.
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

void ModuleHttp::_send_start_response(hsys_module_id_t dest, http_session_result_t result)
{
    MsgHttpStartResponse::Payload p{};
    p.result = result;
    hsys_msg_t *msg = MsgHttpStartResponse::create(id(), p);
    if (msg) send(msg, dest);
}

void ModuleHttp::_send_op_response(hsys_msg_id_t   resp_id,
                                    hsys_module_id_t dest,
                                    http_op_result_t result)
{
    hsys_msg_t *msg = nullptr;

    switch (resp_id) {
        case MSG_ID_HTTP_SET_URL_RESPONSE: {
            MsgHttpSetUrlResponse::Payload p{};
            p.result = result;
            msg = MsgHttpSetUrlResponse::create(id(), p);
            break;
        }
        case MSG_ID_HTTP_SET_ROOT_CA_RESP: {
            MsgHttpSetRootCaResponse::Payload p{};
            p.result = result;
            msg = MsgHttpSetRootCaResponse::create(id(), p);
            break;
        }
        case MSG_ID_HTTP_HEADER_RESPONSE: {
            MsgHttpHeaderResponse::Payload p{};
            p.result = result;
            msg = MsgHttpHeaderResponse::create(id(), p);
            break;
        }
        case MSG_ID_HTTP_BODY_RESPONSE: {
            MsgHttpBodyResponse::Payload p{};
            p.result = result;
            msg = MsgHttpBodyResponse::create(id(), p);
            break;
        }
        default:
            LOG_MSG_ERROR(MOD_HTTP_LOG_EN, "_send_op_response: unknown resp_id 0x%04X",
                          (unsigned)resp_id);
            return;
    }

    if (msg) {
        send(msg, dest);
    }
}

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

bool ModuleHttp::_is_owner(const hsys_msg_t &msg) const
{
    if (_state == IDLE) {
        // Should never happen for configuration messages
        return false;
    }
    if (msg.sender_id != _owner_id) {
        LOG_MSG_WARNING(MOD_HTTP_LOG_EN,
                        "_is_owner: rejected msg 0x%04X from non-owner %u (owner=%u)",
                        (unsigned)msg.msg_id, (unsigned)msg.sender_id, (unsigned)_owner_id);
        return false;
    }
    return true;
}

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
    memset(_hdr_keys,    0, sizeof(_hdr_keys));
    memset(_hdr_vals,    0, sizeof(_hdr_vals));
    memset(_collect_keys, 0, sizeof(_collect_keys));
}
