// module_http.h
//
// ModuleHttp — centralised message-based HTTP/HTTPS client.
//
// Overview
// ────────
// ModuleHttp runs in its own dedicated task (http_task, 10 KB stack).
// Any module that needs HTTP/HTTPS access sends a single MsgHttpRequest
// DIRECT to this module, then waits for MsgHttpResult.
//
// State machine
// ─────────────
//   IDLE      → MsgHttpRequest(any)  → EXECUTING  (task blocks during HTTP call)
//   EXECUTING → PAL call returns     → IDLE        (sends ResponseHeaders + Result)
//
// If MsgHttpRequest arrives while EXECUTING, ModuleHttp immediately replies
// with MsgHttpResult(HTTP_RESULT_BUSY) and stays in EXECUTING.

#pragma once

#include "hsys_module.h"
#include "app_module_ids.h"
#include "http_types.h"
#include "pal_http_client.h"
#include "msg_http_request.h"
#include "FileSystemDriver.h"   /* ota_fs_driver_t */
#include "crc32.h"              /* crc32_update() */
#include <stdint.h>

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#define MODULE_HTTP_NAME  "mod_http"  // exactly 8 chars

// MsgHttpRequest carries at most one collect_key; keep array sized to match.
#define MSG_HTTP_MAX_COLLECT_KEYS  1U

// ---------------------------------------------------------------------------
// ModuleHttp
// ---------------------------------------------------------------------------

class ModuleHttp : public HsysModule
{
public:
    ModuleHttp() : HsysModule(MODULE_HTTP_ID, MODULE_HTTP_NAME) {}

    static ModuleHttp *instance();

protected:
    void init()      override;
    void on_msg_received(const hsys_msg_t &msg) override;

private:
    // ── State ────────────────────────────────────────────────────────────────
    enum State { IDLE, EXECUTING };

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

    // Response header collection
    char    _collect_keys[MSG_HTTP_MAX_COLLECT_KEYS][MODULE_HTTP_MAX_HEADER_KEY] = {};
    uint8_t _collect_count = 0U;

    // Streaming binary sink (optional — set via MsgHttpRequest stream_sink field)
    const ota_fs_driver_t *_stream_drv      = nullptr;
    void                  *_stream_ctx      = nullptr;
    uint32_t               _stream_crc32_expected = 0U;
    bool                   _has_stream_sink = false;

    // ── Message handler ──────────────────────────────────────────────────────
    void _handle_http_request(const hsys_msg_t &msg);

    // ── Execution (runs synchronously, task blocks) ───────────────────────────
    void _execute();

    // ── Send helpers ─────────────────────────────────────────────────────────
    void _send_result(http_result_t result, int32_t status_code,
                      const void *body, uint32_t body_len);
    void _send_response_header(const char *key, const char *value);

    // ── Utility ──────────────────────────────────────────────────────────────
    void _reset_session();
};
