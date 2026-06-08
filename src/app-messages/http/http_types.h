// http_types.h
//
// Shared types, enumerations, and size constants used by the HTTP session
// message classes and ModuleHttp.
//
// Lives in app-messages/http/ so both message headers and the module can
// include it without a circular dependency.

#ifndef HTTP_TYPES_H
#define HTTP_TYPES_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Payload size constants
// ---------------------------------------------------------------------------

#ifndef MODULE_HTTP_MAX_URL_LEN
#define MODULE_HTTP_MAX_URL_LEN          512U   ///< max URL bytes (including NUL)
#endif
#ifndef MODULE_HTTP_MAX_HEADER_KEY
#define MODULE_HTTP_MAX_HEADER_KEY        64U   ///< max header key bytes (including NUL)
#endif
#ifndef MODULE_HTTP_MAX_HEADER_VAL
#define MODULE_HTTP_MAX_HEADER_VAL       256U   ///< max header value bytes (including NUL)
#endif
#ifndef MODULE_HTTP_MAX_REQUEST_BODY
#define MODULE_HTTP_MAX_REQUEST_BODY    4096U   ///< max request body bytes
#endif
#ifndef MODULE_HTTP_MAX_RESPONSE_BODY
// Body is now heap-allocated per-message; this constant caps how many bytes
// ModuleHttp will copy from the response into that heap buffer.
#define MODULE_HTTP_MAX_RESPONSE_BODY   4096U   ///< max response body bytes (heap-allocated per result message)
#endif

// Descriptor payload_size values
#define MODULE_HTTP_PAYLOAD_URL    (4U + MODULE_HTTP_MAX_URL_LEN)
#define MODULE_HTTP_PAYLOAD_HDR    (4U + MODULE_HTTP_MAX_HEADER_KEY + \
                                    4U + MODULE_HTTP_MAX_HEADER_VAL)
// Fixed 16-byte pool slab: body_len(4) + _pad(4) + body_ptr(8).
// The actual body is heap-allocated and freed via msg->cleanup.
#define MODULE_HTTP_PAYLOAD_BODY   16U
// Fixed 16-byte pool slab: result(4) + status_code(4) + body_len(4) + body_ptr(4).
// The actual body is heap-allocated and freed via msg->cleanup.
#define MODULE_HTTP_PAYLOAD_RESULT  16U

// ---------------------------------------------------------------------------
// Idle timer default (ms) — override via CMake compile definition
// ---------------------------------------------------------------------------

#ifndef MODULE_HTTP_IDLE_TIMEOUT_MS
#define MODULE_HTTP_IDLE_TIMEOUT_MS  3000U
#endif

// ---------------------------------------------------------------------------
// http_session_result_t — result of MsgHttpStartRequest
// ---------------------------------------------------------------------------

typedef enum {
    HTTP_SESSION_OK   = 0,  ///< Session opened; caller is now the owner
    HTTP_SESSION_BUSY = 1,  ///< Another session is already open
} http_session_result_t;

// ---------------------------------------------------------------------------
// http_op_result_t — result of individual configuration messages
// ---------------------------------------------------------------------------

typedef enum {
    HTTP_OP_OK               = 0,  ///< Operation accepted
    HTTP_OP_ERR_BAD_SESSION  = 1,  ///< Sender does not own the current session
    HTTP_OP_ERR_URL_TOO_LONG = 2,  ///< URL exceeds MODULE_HTTP_MAX_URL_LEN
    HTTP_OP_ERR_HEADERS_FULL = 3,  ///< PAL_HTTP_MAX_HEADERS already registered
    HTTP_OP_ERR_BAD_STATE    = 4,  ///< Message sent in wrong sequence
} http_op_result_t;

// ---------------------------------------------------------------------------
// http_result_t — outcome carried in MsgHttpResult
// ---------------------------------------------------------------------------

typedef enum {
    HTTP_RESULT_SUCCESS         = 0,  ///< HTTP call completed; status_code valid
    HTTP_RESULT_CONNECT_FAILED  = 1,  ///< TCP/TLS connection could not be established
    HTTP_RESULT_TLS_FAILED      = 2,  ///< TLS handshake or certificate error
    HTTP_RESULT_TIMEOUT         = 3,  ///< Request timed out
    HTTP_RESULT_BODY_TOO_LARGE  = 4,  ///< Response body exceeded MODULE_HTTP_MAX_RESPONSE_BODY
    HTTP_RESULT_SESSION_EXPIRED = 5,  ///< Idle timer fired before MsgHttpSendRequest
    HTTP_RESULT_ERROR           = 6,  ///< Unclassified PAL error
    HTTP_RESULT_BUSY            = 7,  ///< http_task EXECUTING; MsgHttpRequest rejected immediately
} http_result_t;

#endif // HTTP_TYPES_H
