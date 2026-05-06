# module_http — Message-Based HTTP Client Design

**Status:** Approved — ready for implementation  
**Related modules:** `module_cloud`, `module_web_client_ota`, `pal_http_client`

---

## 1. Purpose

Today every module that needs HTTP (cloud, OTA, web client) creates and manages its own
`pal_http_client` handle directly. This has two problems:

- **Transport coupling** — each module knows it is running over ESP32 WiFi. Switching to an
  external modem (Quectel, SIM7xxx) requires changes in every module.
- **Duplication** — TLS setup, timeout handling, error logging, and memory cleanup are
  repeated in each driver.

`module_http` centralises all HTTP/HTTPS access behind a message-based session protocol.
Other modules request a session, configure it, and send the request via messages. The
concrete bearer (ESP32 WiFi, Quectel AT, etc.) is invisible to the caller because
`module_http` only calls `pal_http_client_*()`.

---

## 2. Transport Abstraction

```
module_cloud / module_ota / ...
          │  messages (MsgHttpStartRequest etc.)
          ▼
    [ module_http ]
          │  pal_http_client_*() calls
          ▼
 ┌────────────────────────────────────┐
 │  pal_esp_idf_http_client.cpp       │  ← ESP32 + WiFi (current)
 │  pal_quectel_http_client.cpp       │  ← external modem (future)
 └────────────────────────────────────┘
```

No code above `pal_http_client.h` changes when the bearer changes.

---

## 3. Payload Protocol

Two payload conventions are used.

### 3.1 Fixed payloads

Messages with only a few small fields use a typed C struct.  
The descriptor's `payload_size` equals `sizeof(Payload)`.

### 3.2 Variable-length payloads (4-byte length prefix)

Messages that carry variable-size data (URL, header key/value, request body,
response body) use a raw pool buffer with a length prefix:

```
 byte offset | width    | content
-------------+----------+------------------------------------------
       0..3  | uint32_t | length of the data that follows
       4..N  | N bytes  | actual data
```

The descriptor declares `payload_size = 4 + MAX_DATA_SIZE`.
`MAX_DATA_SIZE` constants are defined in `module_http.h`.

#### Header message layout (key + value, two prefixed fields)

```
 byte offset   | width    | content
---------------+----------+-------------------------------
         0..3  | uint32_t | key_len   (includes NUL)
         4..K  | K bytes  | key (null-terminated)
     K+1..K+4  | uint32_t | value_len (includes NUL)
     K+5..end  | V bytes  | value (null-terminated)
```

`payload_size = 4 + MODULE_HTTP_MAX_HEADER_KEY + 4 + MODULE_HTTP_MAX_HEADER_VAL`

This layout applies to both `MsgHttpHeaderRequest` (client → http) and
`MsgHttpResponseHeader` (http → client).

#### Result message layout

```
 byte offset | width    | content
-------------+----------+--------------------------------------------
       0..3  | uint32_t | http_result_t result
       4..7  | int32_t  | HTTP status code (200, 404 ...; 0 if none)
       8..11 | uint32_t | body_len  (0 when result != SUCCESS)
      12..N  | N bytes  | body (body_len bytes, null-terminated)
```

`payload_size = 12 + MODULE_HTTP_MAX_RESPONSE_BODY`

---

## 4. Session Protocol

All messages are **DIRECT** (point-to-point, not published to the bus).  
The sequence is always: open → configure → send → result.

```
Client                              module_http
  │                                     │
  ├──► MsgHttpStartRequest              │  tell the module which HTTP method and timeout
  │◄── MsgHttpStartResponse             │  SESSION_OK or SESSION_BUSY
  │                                     │
  ├──► MsgHttpSetUrlRequest             │  full URL including scheme (http:// or https://)
  │◄── MsgHttpSetUrlResponse            │
  │                                     │
  ├──► MsgHttpSetRootCaRequest          │  optional: custom CA PEM, or NULL = use cert bundle
  │◄── MsgHttpSetRootCaResponse         │
  │                                     │
  ├──► MsgHttpHeaderRequest             │  repeat for every request header
  │◄── MsgHttpHeaderResponse            │
  │                 ...                 │
  ├──► MsgHttpBodyRequest               │  optional: for POST / PUT only
  │◄── MsgHttpBodyResponse              │
  │                                     │
  ├──► MsgHttpSendRequest               │  trigger execution
  │           ... HTTP executing ...    │  (module_http task blocks on PAL call)
  │◄── MsgHttpResponseHeader            │  [key_len][key][val_len][val]  (one per response header)
  │         (one per response header)   │  all sent before MsgHttpResult
  │◄── MsgHttpResult                    │  [result][status_code][body_len][body]
  │                                     │  session released → IDLE
```

If `MsgHttpAbortRequest` is sent at any point before `MsgHttpSendRequest`, the
session is released immediately with no result message.

---

## 5. Idle Timer

Once `SESSION_OK` is issued, `module_http` starts an idle timer of
`MODULE_HTTP_IDLE_TIMEOUT_MS` (default 3000 ms, configurable at build time).

- Every message received **from the session owner** resets the timer to
  `MODULE_HTTP_IDLE_TIMEOUT_MS`.
- `MsgHttpSendRequest` or `MsgHttpAbortRequest` cancels the timer.
- If the timer fires before either of those, `module_http` sends
  `MsgHttpResult` with `result = HTTP_RESULT_SESSION_EXPIRED` and returns to
  IDLE. The PAL handle is NOT created (no HTTP call was made).

```cpp
// module_http.h
#ifndef MODULE_HTTP_IDLE_TIMEOUT_MS
#define MODULE_HTTP_IDLE_TIMEOUT_MS  3000U   // override in CMakeLists or sdkconfig
#endif
```

---

## 6. Message Table

### 6.1 ID Range: `0x0C00 – 0x0C0D`

| Message                    | ID       | Direction          | Payload type    |
|----------------------------|----------|--------------------|------------------|
| `MsgHttpStartRequest`      | `0x0C00` | client → http      | fixed struct     |
| `MsgHttpStartResponse`     | `0x0C01` | http → client      | fixed struct     |
| `MsgHttpSetUrlRequest`     | `0x0C02` | client → http      | variable         |
| `MsgHttpSetUrlResponse`    | `0x0C03` | http → client      | fixed struct     |
| `MsgHttpSetRootCaRequest`  | `0x0C04` | client → http      | fixed struct     |
| `MsgHttpSetRootCaResponse` | `0x0C05` | http → client      | fixed struct     |
| `MsgHttpHeaderRequest`     | `0x0C06` | client → http      | variable         |
| `MsgHttpHeaderResponse`    | `0x0C07` | http → client      | fixed struct     |
| `MsgHttpBodyRequest`       | `0x0C08` | client → http      | variable         |
| `MsgHttpBodyResponse`      | `0x0C09` | http → client      | fixed struct     |
| `MsgHttpSendRequest`       | `0x0C0A` | client → http      | none (size = 0)  |
| `MsgHttpResult`            | `0x0C0B` | http → client      | variable         |
| `MsgHttpAbortRequest`      | `0x0C0C` | client → http      | none (size = 0)  |
| `MsgHttpResponseHeader`    | `0x0C0D` | http → client      | variable         |

### 6.2 Size Constants (defined in `module_http.h`)

```cpp
#define MODULE_HTTP_MAX_URL_LEN          512
#define MODULE_HTTP_MAX_HEADER_KEY        64
#define MODULE_HTTP_MAX_HEADER_VAL       256
#define MODULE_HTTP_MAX_REQUEST_BODY    4096
#define MODULE_HTTP_MAX_RESPONSE_BODY   2048   // from pool

// Descriptor payload sizes
#define MODULE_HTTP_PAYLOAD_URL    (4 + MODULE_HTTP_MAX_URL_LEN)
#define MODULE_HTTP_PAYLOAD_HDR    (4 + MODULE_HTTP_MAX_HEADER_KEY + \
                                    4 + MODULE_HTTP_MAX_HEADER_VAL)
#define MODULE_HTTP_PAYLOAD_BODY   (4 + MODULE_HTTP_MAX_REQUEST_BODY)
#define MODULE_HTTP_PAYLOAD_RESULT (12 + MODULE_HTTP_MAX_RESPONSE_BODY)
```

### 6.3 Fixed Payload Structs

```cpp
// MsgHttpStartRequest — fixed struct
struct Payload {
    pal_http_method_t  method;      // PAL_HTTP_METHOD_GET / POST / PUT / ...
    uint32_t           timeout_ms;  // 0 = use MODULE_HTTP_IDLE_TIMEOUT_MS
};

// MsgHttpStartResponse — fixed struct
struct Payload {
    http_session_result_t  result;
};

// All other XxxResponse messages — fixed struct
struct Payload {
    http_op_result_t  result;
};

// MsgHttpSetRootCaRequest — fixed struct
// The PEM string is static/flash; only the pointer is stored in the payload.
struct Payload {
    const char *cert_pem;  // NULL = use platform embedded CA bundle
};
```

### 6.4 Enumerations

```c
typedef enum {
    HTTP_SESSION_OK   = 0,
    HTTP_SESSION_BUSY = 1,
} http_session_result_t;

typedef enum {
    HTTP_OP_OK               = 0,
    HTTP_OP_ERR_BAD_SESSION  = 1,   // caller does not own the current session
    HTTP_OP_ERR_URL_TOO_LONG = 2,
    HTTP_OP_ERR_HEADERS_FULL = 3,
    HTTP_OP_ERR_BAD_STATE    = 4,   // message sent in wrong sequence
} http_op_result_t;

typedef enum {
    HTTP_RESULT_SUCCESS         = 0,
    HTTP_RESULT_CONNECT_FAILED  = 1,
    HTTP_RESULT_TLS_FAILED      = 2,
    HTTP_RESULT_TIMEOUT         = 3,
    HTTP_RESULT_BODY_TOO_LARGE  = 4, // response exceeded MODULE_HTTP_RESPONSE_BODY_MAX
    HTTP_RESULT_SESSION_EXPIRED = 5, // 3-second idle timer fired before MsgHttpSendRequest
    HTTP_RESULT_ERROR           = 6, // unclassified PAL error
} http_result_t;
```

---

## 7. module_http State Machine

```
╔══════════════════════════════════════════════════════════════════════════╗
║  IDLE                                                                    ║
║  _owner_id = NONE                                                        ║
╚══════════════════════╦═══════════════════════════════════════════════════╝
                       ║ MsgHttpStartRequest (any sender)
                       ║   _owner_id = msg.sender_id
                       ║   start 3-second idle timer
                       ║   → MsgHttpStartResponse(SESSION_OK)
                       ▼
╔══════════════════════════════════════════════════════════════════════════╗
║  SESSION_OPEN                                                            ║
║  _owner_id set                                                           ║
║                                                                          ║
║  On message FROM owner:                                                  ║
║    SetUrl / SetRootCa / Header / Body  → store, reset timer, send OK     ║
║    MsgHttpSendRequest   → cancel timer  → go EXECUTING                  ║
║    MsgHttpAbortRequest  → cancel timer  → go IDLE                        ║
║                                                                          ║
║  On message FROM other module:                                           ║
║    MsgHttpStartRequest  → MsgHttpStartResponse(SESSION_BUSY)             ║
║    other HTTP messages  → ignore                                         ║
║                                                                          ║
║  On idle timer expiry:                                                   ║
║    → MsgHttpResult(HTTP_RESULT_SESSION_EXPIRED) DIRECT to _owner_id      ║
║    → go IDLE                                                             ║
╚══════════════════════╦═══════════════════════════════════════════════════╝
                       ║ MsgHttpSendRequest
                       ▼
╔══════════════════════════════════════════════════════════════════════════╗
║  EXECUTING                                                               ║
║                                                                          ║
║  pal_http_client_init()                                                  ║
║  pal_http_client_set_url()                                               ║
║  pal_http_client_set_header() × N                                        ║
║  pal_http_client_get() or pal_http_client_post()   ← task blocks here   ║
║  pal_http_client_cleanup()                                               ║
║                                                                          ║
║  For each response header:                                               ║
║    → MsgHttpResponseHeader DIRECT to _owner_id                           ║
║                                                                          ║
║  → MsgHttpResult(result, http_status, body) DIRECT to _owner_id          ║
║  clear _owner_id → go IDLE                                               ║
╚══════════════════════════════════════════════════════════════════════════╝
```

---

## 8. Cloud Module — State Machine Changes

`ModuleCloud` currently calls `cube_sphere_register()` as a blocking function.
With `module_http`, it drives a sub-state machine. Each of the 3 HTTP steps
(nonce → authenticated → nozzle config) goes through its own session cycle.

```
_attempt_registration() → sets _http_step = 0, starts HTTP_ST_START

HTTP_ST_START
  send MsgHttpStartRequest(GET, 30 000 ms)
  → HTTP_ST_WAIT_SESSION

HTTP_ST_WAIT_SESSION
  MsgHttpStartResponse(SESSION_OK) → build URL for step _http_step
                                     send MsgHttpSetUrlRequest
                                     → HTTP_ST_WAIT_URL
  MsgHttpStartResponse(SESSION_BUSY) → retry after 1 s

HTTP_ST_WAIT_URL
  MsgHttpSetUrlResponse(OK) → send MsgHttpSetRootCaRequest(_cloud_root_ca)
                              → HTTP_ST_WAIT_CA

HTTP_ST_WAIT_CA
  MsgHttpSetRootCaResponse(OK) → send first MsgHttpHeaderRequest (if any)
                                  → HTTP_ST_WAIT_HEADERS (or HTTP_ST_WAIT_SEND if none)

HTTP_ST_WAIT_HEADERS  (loop)
  MsgHttpHeaderResponse(OK) → more headers? loop : send MsgHttpSendRequest
                              → HTTP_ST_WAIT_RESULT

HTTP_ST_WAIT_RESULT
  MsgHttpResponseHeader  → capture header value (e.g. nonce token from step 0)
  MsgHttpResult(SUCCESS, 200)
    parse JSON body for _http_step
    _http_step++
    _http_step < 3  → HTTP_ST_START (next request)
    _http_step == 3 → registration complete
  MsgHttpResult(any error)
    → registration failed; schedule retry in MODULE_CLOUD_RETRY_INTERVAL_MS
```

---

## 9. Task Assignment

`module_http` **must run in its own dedicated task** because the `EXECUTING` state
blocks the task for the full duration of the HTTP call (potentially 5–30 seconds).
If it shared a task with other modules those would be starved.

Proposed task table entry:

```cpp
{ "http_task", 10*1024, 5, 0, { MODULE_HTTP_ID, 0 } }
```

10 KB stack is required — same reasoning as `network_task2`: mbedTLS TLS 1.2
handshake (ECDHE key exchange + certificate chain parsing) needs ~6–8 KB of stack
frames on top of FreeRTOS overhead.

`network_task2` drops `MODULE_CLOUD_ID` once the cloud module migration is complete.

---

## 10. OTA Note

`module_web_client_ota` uses `pal_http_client_get_stream()` for binary downloads.
Migrating it to `module_http` requires a streaming `MsgHttpChunk` notification path.
**OTA migration is deferred.** OTA remains disabled until `module_http` is
implemented, tested, and stable with the cloud module. No changes to
`module_web_client_ota` in this phase.

---

## 11. Files to Create / Modify

### New files

```
src/app-messages/http/
    msg_http_start_request.h/.cpp
    msg_http_start_response.h/.cpp
    msg_http_set_url_request.h/.cpp
    msg_http_set_url_response.h/.cpp
    msg_http_set_root_ca_request.h/.cpp
    msg_http_set_root_ca_response.h/.cpp
    msg_http_header_request.h/.cpp
    msg_http_header_response.h/.cpp
    msg_http_body_request.h/.cpp
    msg_http_body_response.h/.cpp
    msg_http_send_request.h/.cpp
    msg_http_result.h/.cpp
    msg_http_abort_request.h/.cpp
    msg_http_response_header.h/.cpp

src/app-modules/module_http/
    module_http.h
    module_http.cpp
```

### Modified files

| File | Change |
|------|--------|
| `app/app_msg_ids.h` | Add `0x0C00–0x0C0D` block |
| `app/app_module_ids.h` | Add `MODULE_HTTP_ID` (ID = 26) |
| `app-modules/app_msg_table.h` | Add all 14 new descriptors |
| `app/app.cpp` | Add `http_task`; remove `MODULE_CLOUD_ID` from `network_task2`; wire module |
| `module_cloud/module_cloud.h/.cpp` | Replace blocking `_attempt_registration()` with message-driven sub-state machine |
| `cube_sphere/cube_sphere_api.cpp` | Simplify or remove — cloud module builds URLs and headers directly |
