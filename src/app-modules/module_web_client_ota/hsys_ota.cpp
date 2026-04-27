/**
 * @file hsys_ota.cpp
 * @brief Cloud-polling OTA engine — PAL-only implementation.
 *
 * Implements:
 *   hsys_ota_check_version()   — POST /api/v1/firmware/check
 *   hsys_ota_download_to_fs()  — GET  /api/v1/firmware/download (streaming)
 *
 * Uses only PAL interfaces:
 *   pal_http_client  — all HTTP communication
 *   pal_logger       — diagnostic output
 *
 * The download writes binary data through ota_fs_driver_t (handed by
 * OtaModule).  No flash write APIs are referenced here.
 *
 * Adapted from the original hsys_ota.cpp in old-app/application/todo/app_ota.
 * Key change: replaced hsys_fw_update_driver_t with ota_fs_driver_t so the
 * engine works with any OTA target (main flash slot OR dispTap SPIFFS file).
 */

#include "hsys_ota.h"
#include "pal_http_client.h"
#include "pal_logger.h"
#include "pal_types.h"
#include "crc32.h"

#include "ArduinoJson.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define __TAG__  "HSYS_OTA"

#define LOG_EN  true

/* =========================================================================
 * hsys_ota_check_version — POST /api/v1/firmware/check
 * ========================================================================= */

int32_t hsys_ota_check_version(const hsys_ota_cfg_t    *cfg,
                                hsys_ota_check_result_t *out)
{
    if (!cfg || !out) return PAL_ERROR;
    memset(out, 0, sizeof(*out));

    /* Build URL */
    char url[HSYS_OTA_MAX_URL_LEN + 64];
    snprintf(url, sizeof(url), "%s/api/v1/firmware/check", cfg->server_url);

    /* Build JSON body */
    char body[320];
    snprintf(body, sizeof(body),
             "{\"device_id\":\"%s\",\"current_version\":\"%s\",\"firmware_type\":\"%s\"}",
             cfg->device_id, cfg->current_version, cfg->firmware_type);

    LOG_MSG_INFO(LOG_EN, "OTA check: %s  fw=%s  ver=%s",
                 url, cfg->firmware_type, cfg->current_version);

    /* Init HTTP client */
    pal_http_client_config_t http_cfg = {};
    http_cfg.url        = url;
    http_cfg.cert_pem   = cfg->cert_pem;
    http_cfg.timeout_ms = (cfg->timeout_ms > 0) ? cfg->timeout_ms : 30000;
    http_cfg.keep_alive = false;

    pal_http_client_handle_t handle = NULL;
    if (pal_http_client_init(&http_cfg, &handle) != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "OTA check: HTTP init failed");
        return PAL_ERROR;
    }

    pal_http_client_set_header(handle, "Content-Type", "application/json");

    pal_http_response_t response = {};
    int32_t status = pal_http_client_post(handle, body, strlen(body), &response);
    pal_http_client_cleanup(handle);

    if (status != 200) {
        LOG_MSG_ERROR(LOG_EN, "OTA check: HTTP %d", (int)status);
        pal_http_response_free(&response);
        return PAL_ERROR;
    }

    if (!response.body || response.body_len == 0) {
        LOG_MSG_ERROR(LOG_EN, "OTA check: empty response");
        pal_http_response_free(&response);
        return PAL_ERROR;
    }

    /* Parse JSON */
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response.body, response.body_len);
    pal_http_response_free(&response);

    if (err) {
        LOG_MSG_ERROR(LOG_EN, "OTA check: JSON error: %s", err.c_str());
        return PAL_ERROR;
    }

    out->update_available = doc["update_available"] | false;

    const char *latest = doc["latest_version"] | "";
    strncpy(out->latest_version, latest, sizeof(out->latest_version) - 1);

    const char *notes = doc["notes"] | "";
    strncpy(out->notes, notes, sizeof(out->notes) - 1);

    if (out->update_available) {
        const char *crc_str = doc["crc32"] | "0x00000000";
        out->crc32      = (uint32_t)strtoul(crc_str, NULL, 16);
        out->file_size  = doc["file_size"] | 0;
        LOG_MSG_INFO(LOG_EN, "OTA check: update available %s → %s  (%lu bytes  CRC=0x%08lX)",
                     cfg->current_version, out->latest_version,
                     (unsigned long)out->file_size, (unsigned long)out->crc32);
    } else {
        LOG_MSG_INFO(LOG_EN, "OTA check: firmware %s is up to date", cfg->firmware_type);
    }

    return PAL_OK;
}

/* =========================================================================
 * Streaming download context
 * ========================================================================= */

typedef struct {
    /* From caller */
    const hsys_ota_cfg_t      *cfg;
    const ota_fs_driver_t     *fs_drv;
    void                      *fs_ctx;
    hsys_ota_progress_cb_t     cb;
    void                      *cb_arg;

    /* Runtime state */
    uint32_t  crc32;
    uint32_t  bytes_received;
    uint32_t  total_bytes;       /* from Content-Length header (may be 0) */
    uint32_t  expected_crc32;    /* from X-CRC32 header (may be 0) */
    bool      fopen_done;
    bool      error;
} _dl_ctx_t;

/**
 * PAL stream chunk callback — receives successive binary blocks and pipes
 * them into the ota_fs_driver_t write session.
 */
static int32_t _stream_chunk_cb(const uint8_t *data, size_t len, void *user_ctx)
{
    _dl_ctx_t *ctx = (_dl_ctx_t *)user_ctx;
    if (ctx->error) return -1;
    if (len == 0)   return 0;

    /* Open the write session on the very first chunk */
    if (!ctx->fopen_done) {
        ota_fs_err_t err = ctx->fs_drv->fopen(ctx->fs_ctx, NULL, OTA_FS_OPEN_WRITE);
        if (err != OTA_FS_OK) {
            LOG_MSG_ERROR(LOG_EN, "OTA dl: fopen failed: %d", (int)err);
            ctx->error = true;
            return -1;
        }
        ctx->fopen_done = true;
    }

    /* Write chunk to the fs driver */
    ota_fs_err_t err = ctx->fs_drv->fwrite(ctx->fs_ctx, data, (uint32_t)len);
    if (err != OTA_FS_OK) {
        LOG_MSG_ERROR(LOG_EN, "OTA dl: fwrite failed: %d", (int)err);
        ctx->error = true;
        return -1;
    }

    /* Update running CRC and byte counter */
    ctx->crc32           = crc32_update(ctx->crc32, data, len);
    ctx->bytes_received += (uint32_t)len;

    /* Fire progress callback */
    if (ctx->cb) {
        uint8_t pct = (ctx->total_bytes > 0)
                      ? (uint8_t)((ctx->bytes_received * 100UL) / ctx->total_bytes)
                      : 0;
        ctx->cb(ctx->bytes_received, ctx->total_bytes, pct, ctx->cb_arg);
    }

    return 0;
}

/* =========================================================================
 * hsys_ota_download_to_fs — GET /api/v1/firmware/download (streaming)
 * ========================================================================= */

int32_t hsys_ota_download_to_fs(const hsys_ota_cfg_t      *cfg,
                                 const char                *version,
                                 const ota_fs_driver_t     *fs_drv,
                                 void                      *fs_ctx,
                                 hsys_ota_progress_cb_t     cb,
                                 void                      *cb_arg)
{
    if (!cfg || !version || !fs_drv || !fs_ctx) return PAL_ERROR;

    if (!fs_drv->fopen || !fs_drv->fwrite || !fs_drv->fclose || !fs_drv->ferase) {
        LOG_MSG_ERROR(LOG_EN, "OTA dl: fs driver missing required ops");
        return PAL_ERROR;
    }

    /* Build download URL */
    char url[HSYS_OTA_MAX_URL_LEN + 192];
    snprintf(url, sizeof(url),
             "%s/api/v1/firmware/download?firmware_type=%s&version=%s&device_id=%s",
             cfg->server_url, cfg->firmware_type, version, cfg->device_id);

    LOG_MSG_INFO(LOG_EN, "OTA dl: starting  fw=%s  ver=%s", cfg->firmware_type, version);

    /* Init HTTP client */
    pal_http_client_config_t http_cfg = {};
    http_cfg.url        = url;
    http_cfg.cert_pem   = cfg->cert_pem;
    http_cfg.timeout_ms = (cfg->timeout_ms > 0) ? cfg->timeout_ms : 120000;
    http_cfg.keep_alive = false;

    pal_http_client_handle_t handle = NULL;
    if (pal_http_client_init(&http_cfg, &handle) != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "OTA dl: HTTP init failed");
        return PAL_ERROR;
    }

    /* Capture CRC32 and Content-Length response headers */
    const char *wanted_headers[] = { "X-CRC32", "Content-Length" };
    pal_http_client_collect_headers(handle, wanted_headers, 2);

    /* Set up streaming context */
    _dl_ctx_t ctx = {};
    ctx.cfg    = cfg;
    ctx.fs_drv = fs_drv;
    ctx.fs_ctx = fs_ctx;
    ctx.cb     = cb;
    ctx.cb_arg = cb_arg;
    ctx.crc32  = 0;

    int32_t http_status = pal_http_client_get_stream(handle, _stream_chunk_cb, &ctx);

    /* Read captured headers */
    char crc_str [32] = {};
    char clen_str[32] = {};
    if (pal_http_client_get_header(handle, "X-CRC32", crc_str, sizeof(crc_str)) == PAL_OK) {
        ctx.expected_crc32 = (uint32_t)strtoul(crc_str, NULL, 16);
    }
    if (pal_http_client_get_header(handle, "Content-Length", clen_str, sizeof(clen_str)) == PAL_OK) {
        ctx.total_bytes = (uint32_t)strtoul(clen_str, NULL, 10);
    }

    pal_http_client_cleanup(handle);

    /* ── Transport / stream error checks ───────────────────────────────── */
    if (http_status < 0) {
        LOG_MSG_ERROR(LOG_EN, "OTA dl: stream error %d", (int)http_status);
        if (ctx.fopen_done) ctx.fs_drv->ferase(ctx.fs_ctx);
        return PAL_ERROR;
    }

    if (http_status != 200) {
        LOG_MSG_ERROR(LOG_EN, "OTA dl: HTTP %d", (int)http_status);
        if (ctx.fopen_done) ctx.fs_drv->ferase(ctx.fs_ctx);
        return PAL_ERROR;
    }

    if (ctx.error || !ctx.fopen_done || ctx.bytes_received == 0) {
        LOG_MSG_ERROR(LOG_EN, "OTA dl: write error or empty body");
        if (ctx.fopen_done) ctx.fs_drv->ferase(ctx.fs_ctx);
        return PAL_ERROR;
    }

    /* ── CRC32 verification ─────────────────────────────────────────────── */
    if (ctx.expected_crc32 != 0 && ctx.crc32 != ctx.expected_crc32) {
        LOG_MSG_ERROR(LOG_EN, "OTA dl: CRC mismatch  got=0x%08lX  expected=0x%08lX",
                      (unsigned long)ctx.crc32, (unsigned long)ctx.expected_crc32);
        ctx.fs_drv->ferase(ctx.fs_ctx);
        return PAL_ERROR;
    }

    LOG_MSG_INFO(LOG_EN, "OTA dl: %lu bytes received  CRC=0x%08lX",
                 (unsigned long)ctx.bytes_received, (unsigned long)ctx.crc32);

    /* ── Commit ─────────────────────────────────────────────────────────── */
    ota_fs_err_t close_err = ctx.fs_drv->fclose(ctx.fs_ctx);
    if (close_err != OTA_FS_OK) {
        LOG_MSG_ERROR(LOG_EN, "OTA dl: fclose failed: %d", (int)close_err);
        return PAL_ERROR;
    }

    LOG_MSG_INFO(LOG_EN, "OTA dl: committed successfully");
    return PAL_OK;
}
