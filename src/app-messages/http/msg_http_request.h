// msg_http_request.h
//
// MsgHttpRequest — unified HTTP acquire+configure+execute message.
//
// Replaces the old burst protocol (StartRequest → StartResponse →
// [SetRootCa, SetUrl, Header×N, Body, SetStreamSink, SendRequest]).
// The caller builds one message and sends it DIRECT to ModuleHttp.
// ModuleHttp replies with MsgHttpResult as before.
//
// Two-allocation pattern (same as MsgFileListSpiffs):
//   • Fixed 64-byte pool-slab (fits 64-byte pool class)
//   • Variable-size data buffer (pool-allocated, freed via msg->cleanup)
//
// ── Slab layout (64 bytes, offset constants below) ──────────────────────
//
//   [HTTPREQ_OFF_DATA_BUF  = 0]   8 bytes  void *data_buf
//   [HTTPREQ_OFF_DATA_LEN  = 8]   4 bytes  uint32_t data_len
//   [HTTPREQ_OFF_METHOD    = 12]  1 byte   uint8_t  method  (pal_http_method_t)
//   [HTTPREQ_OFF_COLLECT_N = 13]  1 byte   uint8_t  collect_count (0 or 1)
//   [HTTPREQ_OFF_PAD1      = 14]  2 bytes  (alignment pad)
//   [HTTPREQ_OFF_TIMEOUT   = 16]  4 bytes  uint32_t timeout_ms
//   [HTTPREQ_OFF_PAD2      = 20]  4 bytes  (pointer alignment pad)
//   [HTTPREQ_OFF_COLL_KEY  = 24]  8 bytes  const char *collect_key (flash lit. or nullptr)
//   [HTTPREQ_OFF_SINK      = 32]  8 bytes  void *stream_sink  (ota_fs_driver_t * or nullptr)
//   [HTTPREQ_OFF_SINK_CTX  = 40]  8 bytes  void *stream_ctx
//   [HTTPREQ_OFF_SINK_CRC  = 48]  4 bytes  uint32_t stream_crc32_expected (0 = skip)
//   [                       52]  12 bytes  (reserved, zeroed)
//
// ── Data buffer binary layout (user spec) ───────────────────────────────
//
//   [8 bytes]    uintptr_t  rootca_ptr  — pointer cast to integer, zero if nullptr
//   [2 bytes]    uint16_t   url_len
//   [url_len]    char       url[]       — NOT NUL-terminated
//   [2 bytes]    uint16_t   headers_len — total packed-header bytes (may be 0)
//   [headers_len] packed headers        — key\0value\0key\0value\0…
//   [2 bytes]    uint16_t   body_len
//   [body_len]   uint8_t    body[]
//
// create() caps body_len at MSG_HTTP_REQUEST_MAX_BODY_LEN (1024) to keep the
// total data buffer within the 2048-byte pool block.

#pragma once

#include "IHsysMsg.h"
#include "hsys_msg.h"
#include "app_msg_ids.h"
#include "http_types.h"
#include "pal_http_client.h"
#include "FileSystemDriver.h"  // ota_fs_driver_t
#include <stdint.h>

// ── Slab size and field offsets ──────────────────────────────────────────────

#define MSG_HTTP_REQUEST_SLAB_SIZE    64U

#define HTTPREQ_OFF_DATA_BUF   0U   ///< void *        — pointer to data buffer
#define HTTPREQ_OFF_DATA_LEN   8U   ///< uint32_t      — data buffer byte count
#define HTTPREQ_OFF_METHOD    12U   ///< uint8_t       — pal_http_method_t
#define HTTPREQ_OFF_COLLECT_N 13U   ///< uint8_t       — collect_count (0 or 1)
#define HTTPREQ_OFF_PAD1      14U   ///< uint8_t[2]    — alignment pad
#define HTTPREQ_OFF_TIMEOUT   16U   ///< uint32_t      — timeout_ms
#define HTTPREQ_OFF_PAD2      20U   ///< uint8_t[4]    — pointer alignment pad
#define HTTPREQ_OFF_COLL_KEY  24U   ///< const char *  — response header to capture
#define HTTPREQ_OFF_SINK      32U   ///< void *        — ota_fs_driver_t *
#define HTTPREQ_OFF_SINK_CTX  40U   ///< void *        — stream context
#define HTTPREQ_OFF_SINK_CRC  48U   ///< uint32_t      — expected CRC32 (0 = skip)

// ── Data-buffer size cap ─────────────────────────────────────────────────────

/// Maximum body bytes accepted by create(). Larger bodies are truncated.
#define MSG_HTTP_REQUEST_MAX_BODY_LEN  1024U

class MsgHttpRequest : public IHsysMsg
{
public:
    static constexpr hsys_msg_id_t ID = MSG_ID_HTTP_REQUEST;

    static constexpr hsys_msg_desc_t DESCRIPTOR =
        HSYS_MSG_DESC(ID,
                      HSYS_MSG_DIRECT,
                      (uint16_t)MSG_HTTP_REQUEST_SLAB_SIZE,
                      HSYS_PERM_ANY,
                      HSYS_PERM_ANY);

    // ── Zero-copy view returned by parse() ──────────────────────────────────

    struct ParsedFields {
        const char           *rootca;         ///< TLS CA cert (may be nullptr → PAL bundle)
        const char           *url;            ///< NOT NUL-terminated; use url_len
        uint16_t              url_len;
        const uint8_t        *headers_buf;    ///< Packed key\0val\0 pairs (may be nullptr)
        uint16_t              headers_len;
        const uint8_t        *body;           ///< Request body (may be nullptr)
        uint16_t              body_len;
        pal_http_method_t     method;
        uint32_t              timeout_ms;
        const char           *collect_key;    ///< Response header to capture (may be nullptr)
        uint8_t               collect_count;  ///< 0 or 1
        void                 *stream_sink;    ///< ota_fs_driver_t * or nullptr
        void                 *stream_ctx;
        uint32_t              stream_crc32;   ///< Expected CRC32 (0 = skip)
    };

    MsgHttpRequest() = default;
    hsys_msg_id_t msg_id() const override { return ID; }
    void serialize(hsys_msg_t *) const override {}

    /**
     * Create a MsgHttpRequest.
     *
     * @param sender_id      Sending module (used in MsgHttpResult routing).
     * @param method         HTTP method.
     * @param timeout_ms     Request timeout; 0 → PAL default (30 s).
     * @param rootca         TLS CA cert PEM string (nullptr → PAL bundle).
     * @param url            Request URL (NUL-terminated, must be non-null).
     * @param headers_buf    Packed key\0val\0 pairs (nullptr if no extra headers).
     * @param headers_len    Byte count of headers_buf (0 if nullptr).
     * @param body           Request body (nullptr for GET / HEAD).
     * @param body_len       Body byte count; truncated to MSG_HTTP_REQUEST_MAX_BODY_LEN.
     * @param collect_key    Response header name to capture (nullptr for none).
     * @param stream_sink    OTA fs driver for streaming GET; nullptr for buffered.
     * @param stream_ctx     Passed verbatim to the fs driver ops.
     * @param stream_crc32   Expected CRC32 of streamed data (0 = skip check).
     * @return               Ready-to-send message, or nullptr on pool exhaustion.
     */
    static hsys_msg_t *create(hsys_module_id_t       sender_id,
                               pal_http_method_t      method,
                               uint32_t               timeout_ms,
                               const char            *rootca,
                               const char            *url,
                               const void            *headers_buf,
                               uint16_t               headers_len,
                               const void            *body,
                               uint16_t               body_len,
                               const char            *collect_key,
                               void                  *stream_sink  = nullptr,
                               void                  *stream_ctx   = nullptr,
                               uint32_t               stream_crc32 = 0U);

    /**
     * Parse a received MsgHttpRequest.  Zero-copy: all pointers reference
     * memory inside the data_buf for the lifetime of the message.
     */
    static ParsedFields parse(const hsys_msg_t &msg);

    /** JSON codec — from_json builds a request from logical fields. */
    static hsys_msg_t *from_json(const char *data_json, hsys_module_id_t sender_id);

    /** JSON codec — to_json serialises the logical fields (rootca/stream skipped). */
    static int32_t to_json(const hsys_msg_t *msg, char *buf, uint32_t buf_len);
};
