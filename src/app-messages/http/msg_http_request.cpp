// msg_http_request.cpp
//
// See msg_http_request.h for layout documentation.

#define __TAG__  "HTTP_REQ"

#include "msg_http_request.h"
#include "hsys_pool.h"
#include "pal_logger.h"
#include <string.h>
#include <stdint.h>

#ifndef MSG_HTTP_REQ_LOG_EN
#define MSG_HTTP_REQ_LOG_EN  true
#endif

// ── Data-buffer offset constants (within the variable data_buf) ───────────────

static constexpr uint16_t k_db_rootca  = 0U;    // uintptr_t — 8 bytes
static constexpr uint16_t k_db_urllen  = 8U;    // uint16_t  — 2 bytes
static constexpr uint16_t k_db_url     = 10U;   // char[]    — url_len bytes

// ── Cleanup hook — frees data_buf when ref_count reaches 0 ───────────────────

static void _http_request_cleanup(void *payload)
{
    if (!payload) return;
    void *data_buf = nullptr;
    memcpy(&data_buf, (uint8_t *)payload + HTTPREQ_OFF_DATA_BUF, sizeof(void *));
    if (data_buf) hsys_pool_free(data_buf);
}

// ── create() ─────────────────────────────────────────────────────────────────

hsys_msg_t *MsgHttpRequest::create(hsys_module_id_t  sender_id,
                                    pal_http_method_t method,
                                    uint32_t          timeout_ms,
                                    const char       *rootca,
                                    const char       *url,
                                    const void       *headers_buf,
                                    uint16_t          headers_len,
                                    const void       *body,
                                    uint16_t          body_len,
                                    const char       *collect_key,
                                    void             *stream_sink,
                                    void             *stream_ctx,
                                    uint32_t          stream_crc32)
{
    if (!url || url[0] == '\0') {
        LOG_MSG_ERROR(MSG_HTTP_REQ_LOG_EN, "create: url is required");
        return nullptr;
    }

    // Clamp body to cap
    if (body_len > MSG_HTTP_REQUEST_MAX_BODY_LEN) {
        LOG_MSG_WARNING(MSG_HTTP_REQ_LOG_EN,
                        "create: body_len %u capped to %u",
                        (unsigned)body_len, (unsigned)MSG_HTTP_REQUEST_MAX_BODY_LEN);
        body_len = MSG_HTTP_REQUEST_MAX_BODY_LEN;
    }
    if (!body) body_len = 0;
    if (!headers_buf) headers_len = 0;

    uint16_t url_len = (uint16_t)strnlen(url, MODULE_HTTP_MAX_URL_LEN);

    // Compute total data buffer size:
    //   8 (rootca ptr) + 2 (url_len) + url_len
    // + 2 (headers_len) + headers_len
    // + 2 (body_len) + body_len
    uint32_t total = 8U + 2U + url_len + 2U + headers_len + 2U + body_len;

    if (total > 2048U) {
        LOG_MSG_ERROR(MSG_HTTP_REQ_LOG_EN,
                      "create: data buffer %lu bytes > 2048 pool max",
                      (unsigned long)total);
        return nullptr;
    }

    // Allocate data buffer
    uint8_t *db = static_cast<uint8_t *>(hsys_pool_alloc((uint16_t)total));
    if (!db) {
        LOG_MSG_ERROR(MSG_HTTP_REQ_LOG_EN,
                      "create: pool alloc failed for %lu bytes", (unsigned long)total);
        return nullptr;
    }
    memset(db, 0, total);

    // Fill data buffer
    {
        uintptr_t rca_int = (uintptr_t)rootca;
        memcpy(db + k_db_rootca, &rca_int, 8U);
        memcpy(db + k_db_urllen, &url_len, 2U);
        memcpy(db + k_db_url,    url,      url_len);

        uint16_t off = k_db_url + url_len;
        memcpy(db + off, &headers_len, 2U); off += 2U;
        if (headers_len > 0U) { memcpy(db + off, headers_buf, headers_len); }
        off += headers_len;

        memcpy(db + off, &body_len, 2U); off += 2U;
        if (body_len > 0U) { memcpy(db + off, body, body_len); }
    }

    // Allocate slab
    hsys_msg_t *msg = hsys_msg_create(ID, sender_id);
    if (!msg || !msg->payload) {
        LOG_MSG_ERROR(MSG_HTTP_REQ_LOG_EN, "create: hsys_msg_create failed");
        hsys_pool_free(db);
        return nullptr;
    }

    // Fill slab using offset constants
    uint8_t *slab = static_cast<uint8_t *>(msg->payload);
    memset(slab, 0, MSG_HTTP_REQUEST_SLAB_SIZE);

    {
        void *db_ptr = db;
        memcpy(slab + HTTPREQ_OFF_DATA_BUF, &db_ptr,  sizeof(void *));
        memcpy(slab + HTTPREQ_OFF_DATA_LEN, &total,   sizeof(uint32_t));
        uint8_t m = (uint8_t)method;
        memcpy(slab + HTTPREQ_OFF_METHOD,   &m,       1U);
        uint8_t cc = (collect_key && collect_key[0] != '\0') ? 1U : 0U;
        memcpy(slab + HTTPREQ_OFF_COLLECT_N, &cc,     1U);
        memcpy(slab + HTTPREQ_OFF_TIMEOUT,  &timeout_ms, sizeof(uint32_t));

        const void *ck = collect_key;
        memcpy(slab + HTTPREQ_OFF_COLL_KEY, &ck,      sizeof(void *));

        memcpy(slab + HTTPREQ_OFF_SINK,     &stream_sink, sizeof(void *));
        memcpy(slab + HTTPREQ_OFF_SINK_CTX, &stream_ctx,  sizeof(void *));
        memcpy(slab + HTTPREQ_OFF_SINK_CRC, &stream_crc32, sizeof(uint32_t));
    }

    msg->cleanup = _http_request_cleanup;
    return msg;
}

// ── parse() ───────────────────────────────────────────────────────────────────

MsgHttpRequest::ParsedFields MsgHttpRequest::parse(const hsys_msg_t &msg)
{
    ParsedFields f = {};
    if (!msg.payload) return f;

    const uint8_t *slab = static_cast<const uint8_t *>(msg.payload);

    // Read slab fields
    void *db_ptr = nullptr;
    memcpy(&db_ptr, slab + HTTPREQ_OFF_DATA_BUF, sizeof(void *));

    uint32_t data_len = 0U;
    memcpy(&data_len, slab + HTTPREQ_OFF_DATA_LEN, sizeof(uint32_t));

    uint8_t method_byte = 0U;
    memcpy(&method_byte, slab + HTTPREQ_OFF_METHOD, 1U);
    f.method = (pal_http_method_t)method_byte;

    memcpy(&f.collect_count, slab + HTTPREQ_OFF_COLLECT_N, 1U);
    memcpy(&f.timeout_ms,    slab + HTTPREQ_OFF_TIMEOUT,   sizeof(uint32_t));

    const void *ck = nullptr;
    memcpy(&ck, slab + HTTPREQ_OFF_COLL_KEY, sizeof(void *));
    f.collect_key = static_cast<const char *>(ck);

    memcpy(&f.stream_sink,   slab + HTTPREQ_OFF_SINK,     sizeof(void *));
    memcpy(&f.stream_ctx,    slab + HTTPREQ_OFF_SINK_CTX, sizeof(void *));
    memcpy(&f.stream_crc32,  slab + HTTPREQ_OFF_SINK_CRC, sizeof(uint32_t));

    if (!db_ptr || data_len < (8U + 2U + 2U + 2U)) return f;

    const uint8_t *db = static_cast<const uint8_t *>(db_ptr);

    // rootca
    uintptr_t rca_int = 0U;
    memcpy(&rca_int, db + k_db_rootca, 8U);
    f.rootca = rca_int ? reinterpret_cast<const char *>(rca_int) : nullptr;

    // url
    uint16_t url_len = 0U;
    memcpy(&url_len, db + k_db_urllen, 2U);
    f.url     = reinterpret_cast<const char *>(db + k_db_url);
    f.url_len = url_len;

    uint16_t off = k_db_url + url_len;

    // headers
    uint16_t hdr_len = 0U;
    memcpy(&hdr_len, db + off, 2U); off += 2U;
    f.headers_buf = (hdr_len > 0U) ? (db + off) : nullptr;
    f.headers_len = hdr_len;
    off += hdr_len;

    // body
    uint16_t body_len = 0U;
    memcpy(&body_len, db + off, 2U); off += 2U;
    f.body     = (body_len > 0U) ? (db + off) : nullptr;
    f.body_len = body_len;

    return f;
}

// ── JSON codec ────────────────────────────────────────────────────────────────

hsys_msg_t *MsgHttpRequest::from_json(const char * /*data_json*/,
                                       hsys_module_id_t /*sender_id*/)
{
    // Minimal implementation: rootca and stream_sink are not representable
    // in JSON. The tool uses this path only for display; returning nullptr is safe.
    return nullptr;
}

int32_t MsgHttpRequest::to_json(const hsys_msg_t *msg, char *buf, uint32_t buf_len)
{
    if (!msg || !buf || buf_len < 3U) return 0;

    ParsedFields f = parse(*msg);

    // Build a null-terminated copy of the URL for output
    char url_str[MODULE_HTTP_MAX_URL_LEN] = {};
    if (f.url && f.url_len > 0U) {
        uint16_t copy = (f.url_len < sizeof(url_str) - 1U)
                        ? f.url_len : (uint16_t)(sizeof(url_str) - 1U);
        memcpy(url_str, f.url, copy);
    }

    int32_t written = snprintf(buf, buf_len,
        "{\"method\":%d,\"timeout_ms\":%lu,\"url\":\"%s\"}",
        (int)f.method,
        (unsigned long)f.timeout_ms,
        url_str);

    return (written > 0) ? written : 0;
}
