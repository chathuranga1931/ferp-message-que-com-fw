// module_http.h
//
// ModuleHttp — centralised message-based HTTP/HTTPS client.
//
// Overview
// ────────
// ModuleHttp runs in its own dedicated task (http_task, 10 KB stack).
// Any module that needs HTTP/HTTPS access opens a session via messages
// instead of calling pal_http_client_*() directly.
//
// State machine
// ─────────────
//   IDLE          → MsgHttpStartRequest(any)    → SESSION_OPEN
//   SESSION_OPEN  → MsgHttpSendRequest(owner)   → EXECUTING  (task blocks here)
//   SESSION_OPEN  → MsgHttpAbortRequest(owner)  → IDLE
//   SESSION_OPEN  → idle timer expiry           → IDLE (sends SESSION_EXPIRED)
//   EXECUTING     → PAL call returns            → IDLE (sends ResponseHeaders + Result)
//
// Idle timer
// ──────────
// Default MODULE_HTTP_IDLE_TIMEOUT_MS ms (3 s).  Resets on every message from
// the session owner.  Cancelled when MsgHttpSendRequest or MsgHttpAbortRequest
// arrives.  Override the default at compile time:
//   target_compile_definitions(... PRIVATE MODULE_HTTP_IDLE_TIMEOUT_MS=5000)

#pragma once

#include "hsys_module.h"
#include "hsys_soft_timer.h"
#include "app_module_ids.h"
#include "http_types.h"
#include "pal_http_client.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#define MODULE_HTTP_NAME  "mod_http"  // exactly 8 chars

// ---------------------------------------------------------------------------
// ModuleHttp
// ---------------------------------------------------------------------------

class ModuleHttp : public HsysModule
{
public:
    ModuleHttp() : HsysModule(MODULE_HTTP_ID, MODULE_HTTP_NAME) {}

    static ModuleHttp *instance();

protected:
    void pre_init()  override;
    void init()      override;
    void on_msg_received(const hsys_msg_t &msg) override;
    void on_wake()   override;

private:
    // ── State ────────────────────────────────────────────────────────────────
    enum State { IDLE, SESSION_OPEN, EXECUTING };

    State            _state    = IDLE;
    hsys_module_id_t _owner_id = HSYS_MODULE_ID_INVALID;

    // Session configuration
    pal_http_method_t _method      = PAL_HTTP_METHOD_GET;
    uint32_t          _timeout_ms  = 0U;
    const char       *_cert_pem    = nullptr;
    char              _url[MODULE_HTTP_MAX_URL_LEN]     = {};
    char              _hdr_keys[PAL_HTTP_MAX_HEADERS][MODULE_HTTP_MAX_HEADER_KEY] = {};
    char              _hdr_vals[PAL_HTTP_MAX_HEADERS][MODULE_HTTP_MAX_HEADER_VAL] = {};
    uint8_t           _hdr_count   = 0U;
    uint8_t           _body_buf[MODULE_HTTP_MAX_REQUEST_BODY] = {};
    uint32_t          _body_len    = 0U;

    // ── Idle timer ───────────────────────────────────────────────────────────
    hsys_timer_handle_t _idle_timer    = nullptr;
    volatile bool       _idle_expired  = false;

    static void _idle_timer_cb(void *timer_handle);
    void        _on_idle_timer_fired();
    void        _reset_idle_timer();
    void        _cancel_idle_timer();

    // ── Message handlers ─────────────────────────────────────────────────────
    void _handle_start_request(const hsys_msg_t &msg);
    void _handle_set_url(const hsys_msg_t &msg);
    void _handle_set_root_ca(const hsys_msg_t &msg);
    void _handle_header(const hsys_msg_t &msg);
    void _handle_body(const hsys_msg_t &msg);
    void _handle_send(const hsys_msg_t &msg);
    void _handle_abort(const hsys_msg_t &msg);

    // ── Execution (runs synchronously, task blocks) ───────────────────────────
    void _execute();

    // ── Send helpers ─────────────────────────────────────────────────────────
    void _send_start_response(hsys_module_id_t dest, http_session_result_t result);
    void _send_op_response(hsys_msg_id_t resp_id, hsys_module_id_t dest, http_op_result_t result);
    void _send_result(http_result_t result, int32_t status_code,
                      const void *body, uint32_t body_len);
    void _send_response_header(const char *key, const char *value);

    // ── Utility ──────────────────────────────────────────────────────────────
    bool _is_owner(const hsys_msg_t &msg) const;
    void _reset_session();
};
