/**
 * @file hsys_ota.cpp
 * @brief OTA check and download driver — PAL-only implementation
 *
 * Provides the two functions assigned into hsys_ota_driver_t:
 *
 *   drv->fp_check_version      = pal_esp_idf_ota_check_version;
 *   drv->fp_download_and_flash = pal_esp_idf_ota_download_and_flash;
 *
 * Uses ONLY PAL interfaces:
 *   - pal_http_client  — for all HTTP communication (POST check + GET stream)
 *   - pal_fw_update    — for writing firmware to the OTA flash slot
 *   - pal_logger       — for log output
 *
 * No ESP-IDF headers are included here.
 *
 * CRC32: poly 0xEDB88320, initial 0, no final XOR — matches server calc_crc.py.
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

#define HSYS_OTA_DEBUG_LOG_EN   LOG_DIS
#define HSYS_OTA_WARN_LOG_EN    LOG_DIS
#define HSYS_OTA_ERROR_LOG_EN   LOG_DIS
#define HSYS_OTA_INFO_LOG_EN    LOG_DIS

/* =========================================================================
 * fp_check_version — POST /api/v1/firmware/check
 * ========================================================================= */

int32_t pal_esp_idf_ota_check_version(hsys_ota_driver_t* drv,
                                       hsys_ota_result_t* result)
{
    if (!drv || !result) return PAL_ERROR;
    memset(result, 0, sizeof(*result));

    hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_CHECK_STARTED, NULL);

    /* Build URL */
    char url[PAL_OTA_MAX_URL_LEN + 64];
    snprintf(url, sizeof(url), "%s/api/v1/firmware/check",
             drv->fp_get_server_url ? drv->fp_get_server_url() : "");

    /* Build JSON body */
    char body[256];
    snprintf(body, sizeof(body),
             "{\"device_id\":\"%s\",\"current_version\":\"%s\",\"firmware_type\":\"%s\"}",
             drv->fp_get_device_id       ? drv->fp_get_device_id()       : "",
             drv->fp_get_current_version ? drv->fp_get_current_version() : "",
             drv->fp_get_firmware_type   ? drv->fp_get_firmware_type()   : "");
    
    LOG_MSG_DEBUG(HSYS_OTA_DEBUG_LOG_EN, "Checking for OTA update at %s", url);
    LOG_MSG_DEBUG(HSYS_OTA_DEBUG_LOG_EN, "Request body: %s", body);

    /* Init PAL HTTP client */
    pal_http_client_config_t http_cfg = {};
    http_cfg.url        = url;
    http_cfg.cert_pem   = drv->fp_get_cert_pem ? drv->fp_get_cert_pem() : NULL;
    http_cfg.timeout_ms = (drv->fp_get_timeout_ms && drv->fp_get_timeout_ms() > 0)
                          ? drv->fp_get_timeout_ms() : 30000;
    http_cfg.keep_alive = false;

    pal_http_client_handle_t handle = NULL;
    if (pal_http_client_init(&http_cfg, &handle) != PAL_OK) 
    {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA check: HTTP init failed");
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"HTTP init failed");
        return PAL_ERROR;
    }

    pal_http_client_set_header(handle, "Content-Type", "application/json");

    pal_http_response_t response = {};
    int32_t status = pal_http_client_post(handle, body, strlen(body), &response);
    pal_http_client_cleanup(handle);

    if (status != 200) 
    {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA check: HTTP %d", (int)status);
        pal_http_response_free(&response);
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"HTTP check failed");
        return PAL_ERROR;
    }

    if (!response.body || response.body_len == 0) 
    {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA check: empty response");
        pal_http_response_free(&response);
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"Empty server response");
        return PAL_ERROR;
    }

    LOG_MSG_DEBUG(HSYS_OTA_DEBUG_LOG_EN, "OTA check response: %s", response.body);

    /* Parse JSON */
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response.body, response.body_len);
    pal_http_response_free(&response);

    if (err) 
    {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA check: JSON parse error: %s", err.c_str());
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"JSON parse error");
        return PAL_ERROR;
    }

    result->update_available = doc["update_available"] | false;

    const char* latest = doc["latest_version"] | "";
    strncpy(result->latest_version, latest, PAL_OTA_MAX_VERSION_LEN - 1);

    const char* fw_type = doc["firmware_type"] | "";
    strncpy(result->firmware_type, fw_type, PAL_OTA_MAX_FW_TYPE_LEN - 1);

    const char* notes = doc["notes"] | "";
    strncpy(result->notes, notes, PAL_OTA_MAX_NOTES_LEN - 1);

    if (result->update_available) 
    {
        const char* crc_str = doc["crc32"] | "0x00000000";
        result->crc32     = (uint32_t)strtoul(crc_str, NULL, 16);
        result->file_size = doc["file_size"] | 0;
        LOG_MSG_INFO(HSYS_OTA_DEBUG_LOG_EN, "Update available: %s → %s  CRC32=0x%08lX  size=%lu",
                     drv->fp_get_current_version ? drv->fp_get_current_version() : "?",
                     result->latest_version,
                     (unsigned long)result->crc32, (unsigned long)result->file_size);
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_UPDATE_AVAILABLE, result);
    } 
    else 
    {
        result->crc32     = 0;
        result->file_size = 0;
        LOG_MSG_INFO(HSYS_OTA_DEBUG_LOG_EN, "Firmware up to date (%s)",
                     drv->fp_get_current_version ? drv->fp_get_current_version() : "?");
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_NO_UPDATE, NULL);
    }

    return PAL_OK;
}

/* =========================================================================
 * Streaming download context — used by the pal_http_stream_chunk_cb_t
 * ========================================================================= */

typedef struct {
    hsys_ota_driver_t*      drv;
    hsys_fw_update_driver_t* fw_drv;          /* convenience alias — drv->fw_drv */
    pal_fw_update_handle_t  fw_handle;
    uint32_t                crc32;
    uint32_t                bytes_received;
    uint32_t                total_bytes;      /* populated from Content-Length header */
    uint32_t                expected_crc32;   /* populated from X-CRC32 header */
    bool                    begin_done;
    bool                    error;
} _dl_ctx_t;

/**
 * PAL stream chunk callback — receives successive data blocks from
 * pal_http_client_get_stream() and pipes them into the fw_drv write session.
 */
static int32_t _stream_chunk_cb(const uint8_t* data, size_t len, void* user_ctx)
{
    _dl_ctx_t* ctx = (_dl_ctx_t*)user_ctx;
    if (ctx->error) return -1;
    if (len == 0)   return 0;

    /* Open flash-write session on first chunk */
    if (!ctx->begin_done) {
        if (ctx->fw_drv->fp_begin(&ctx->fw_handle) != PAL_OK) {
            LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "fw_drv->fp_begin failed");
            ctx->error = true;
            return -1;
        }
        ctx->begin_done = true;
        LOG_MSG_DEBUG(HSYS_OTA_DEBUG_LOG_EN, "OTA write session opened");
    }

    if (ctx->fw_drv->fp_write(ctx->fw_handle, data, len) != PAL_OK) {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "fw_drv->fp_write failed at offset %lu",
                      (unsigned long)ctx->bytes_received);
        ctx->error = true;
        return -1;
    }

    ctx->crc32           = crc32_update(ctx->crc32, data, len);
    ctx->bytes_received += (uint32_t)len;

    /* Emit progress event */
    hsys_ota_progress_t prog = {};
    prog.bytes_received = ctx->bytes_received;
    prog.total_bytes    = ctx->total_bytes;
    prog.crc32_running  = ctx->crc32;
    prog.percent        = (ctx->total_bytes > 0)
                          ? (uint8_t)((ctx->bytes_received * 100UL) / ctx->total_bytes)
                          : 0;
    hsys_ota_emit(ctx->drv, PAL_OTA_CHECK_EVENT_DOWNLOAD_PROGRESS, &prog);

    return 0;
}

/* =========================================================================
 * fp_download_and_flash — GET /api/v1/firmware/download (streaming)
 * ========================================================================= */

int32_t pal_esp_idf_ota_download_and_flash(hsys_ota_driver_t* drv,
                                            const char*             version)
{
    if (!drv || !version) return PAL_ERROR;

    if (!drv->fw_drv || !drv->fw_drv->fp_begin ||
        !drv->fw_drv->fp_write || !drv->fw_drv->fp_end || !drv->fw_drv->fp_abort)
    {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA download: fw_drv or its function pointers are NULL");
        return PAL_ERROR;
    }

    /* Build download URL */
    char url[PAL_OTA_MAX_URL_LEN + 128];
    snprintf(url, sizeof(url),
             "%s/api/v1/firmware/download?firmware_type=%s&version=%s&device_id=%s",
             drv->fp_get_server_url    ? drv->fp_get_server_url()    : "",
             drv->fp_get_firmware_type ? drv->fp_get_firmware_type() : "",
             version,
             drv->fp_get_device_id     ? drv->fp_get_device_id()     : "");

    LOG_MSG_INFO(HSYS_OTA_DEBUG_LOG_EN, "Starting firmware download: %s", url);
    hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_DOWNLOAD_STARTED, NULL);

    /* Init PAL HTTP client */
    pal_http_client_config_t http_cfg = {};
    http_cfg.url        = url;
    http_cfg.cert_pem   = drv->fp_get_cert_pem ? drv->fp_get_cert_pem() : NULL;
    http_cfg.timeout_ms = (drv->fp_get_timeout_ms && drv->fp_get_timeout_ms() > 0)
                          ? drv->fp_get_timeout_ms() : 120000;
    http_cfg.keep_alive = false;

    pal_http_client_handle_t handle = NULL;
    if (pal_http_client_init(&http_cfg, &handle) != PAL_OK) {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA download: HTTP init failed");
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"HTTP init failed");
        return PAL_ERROR;
    }

    /* Register response headers to capture */
    const char* wanted_headers[] = { "X-CRC32", "Content-Length" };
    pal_http_client_collect_headers(handle, wanted_headers, 2);

    /* Set up streaming context */
    _dl_ctx_t ctx = {};
    ctx.drv    = drv;
    ctx.fw_drv = drv->fw_drv;
    ctx.crc32  = 0;

    int32_t http_status = pal_http_client_get_stream(handle, _stream_chunk_cb, &ctx);

    /* Read captured response headers */
    char crc_str[32]  = {};
    char clen_str[32] = {};
    if (pal_http_client_get_header(handle, "X-CRC32", crc_str, sizeof(crc_str)) == PAL_OK) {
        ctx.expected_crc32 = (uint32_t)strtoul(crc_str, NULL, 16);
        LOG_MSG_DEBUG(HSYS_OTA_DEBUG_LOG_EN, "Server CRC32: 0x%08lX", (unsigned long)ctx.expected_crc32);
    }
    if (pal_http_client_get_header(handle, "Content-Length", clen_str, sizeof(clen_str)) == PAL_OK) {
        ctx.total_bytes = (uint32_t)strtoul(clen_str, NULL, 10);
        LOG_MSG_DEBUG(HSYS_OTA_DEBUG_LOG_EN, "Content-Length: %lu", (unsigned long)ctx.total_bytes);
    }

    pal_http_client_cleanup(handle);

    /* --- Transport-level checks --- */
    if (http_status < 0) {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA download: transport error");
        if (ctx.begin_done) ctx.fw_drv->fp_abort(ctx.fw_handle);
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"HTTP download failed");
        return PAL_ERROR;
    }

    if (http_status != 200) {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA download: server returned HTTP %d", (int)http_status);
        if (ctx.begin_done) ctx.fw_drv->fp_abort(ctx.fw_handle);
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"Server returned non-200");
        return PAL_ERROR;
    }

    if (ctx.error || !ctx.begin_done || ctx.bytes_received == 0) {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "OTA download: no data or write error");
        if (ctx.begin_done) ctx.fw_drv->fp_abort(ctx.fw_handle);
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"Download/write error");
        return PAL_ERROR;
    }

    /* --- CRC32 verification --- */
    if (ctx.expected_crc32 != 0) {
        if (ctx.crc32 != ctx.expected_crc32) {
            LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "CRC32 mismatch: got 0x%08lX expected 0x%08lX",
                          (unsigned long)ctx.crc32,
                          (unsigned long)ctx.expected_crc32);
            ctx.fw_drv->fp_abort(ctx.fw_handle);
            hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"CRC32 mismatch");
            return PAL_ERROR;
        }
        LOG_MSG_INFO(HSYS_OTA_DEBUG_LOG_EN, "CRC32 verified: 0x%08lX ✓", (unsigned long)ctx.crc32);
    } else {
        LOG_MSG_WARNING(HSYS_OTA_DEBUG_LOG_EN, "No X-CRC32 header — skipping CRC check");
    }

    LOG_MSG_INFO(HSYS_OTA_DEBUG_LOG_EN, "Download complete: %lu bytes", (unsigned long)ctx.bytes_received);

    /* --- Commit to OTA slot --- */
    int32_t ret = ctx.fw_drv->fp_end(ctx.fw_handle);
    if (ret != PAL_OK) {
        LOG_MSG_ERROR(HSYS_OTA_DEBUG_LOG_EN, "fw_drv->fp_end failed: %d", (int)ret);
        hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_ERROR, (void*)"Flash commit failed");
        return PAL_ERROR;
    }

    LOG_MSG_INFO(HSYS_OTA_DEBUG_LOG_EN, "Firmware written and committed — caller must call pal_power_reset()");
    hsys_ota_emit(drv, PAL_OTA_CHECK_EVENT_DOWNLOAD_COMPLETE, NULL);
    return PAL_OK;
}

