/**
 * @file ota_driver_esp32_dt.c
 * @brief OTA filesystem driver for the ESP32 dispTap (DT) binary files.
 *
 * Streams each received binary into the DT image store (app_dt_image), which
 * keeps the images on the SD card (/dtfw/<name>).  Without a mounted card
 * fopen() fails, so DT firmware OTA is not available.
 *
 *   fopen  → app_dt_image_write_begin()  (writes to "<name>.tmp")
 *   fwrite → app_dt_image_write()        streams each incoming chunk
 *   fclose → app_dt_image_write_commit() verifies (size + CRC32) and swaps
 *            the new image in — the old image stays usable until then
 *   ferase → app_dt_image_write_abort()  discards the partial file
 *   fread  → not supported
 *
 * ctx->spiffs_path names the image, e.g. "esp32/distap_esp32.bin"; only the
 * file name part is used.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_driver_esp32_dt.h"
#include "app_dt_image.h"
#include "pal_logger.h"

#define __TAG__  "OTA_DT  "
#define LOG_EN   true

/* -------------------------------------------------------------------------
 * Driver function implementations
 * ------------------------------------------------------------------------- */

static ota_fs_err_t _dt_fopen(void *ctx, const char *path, ota_fs_open_mode_t mode)
{
    (void)path;   /* path comes from ctx->spiffs_path */
    (void)mode;   /* always write mode */

    ota_esp32_dt_ctx_t *c = (ota_esp32_dt_ctx_t *)ctx;
    if (!c || !c->spiffs_path) return OTA_FS_ERR_INVALID_ARG;

    if (app_dt_image_write_begin(c->spiffs_path) != APP_DT_IMAGE_OK) {
        LOG_MSG_ERROR(LOG_EN, "fopen: cannot start %s", c->spiffs_path);
        return OTA_FS_ERR_WRITE_FAIL;
    }

    c->is_open = true;
    LOG_MSG_INFO(LOG_EN, "fopen: dispTap OTA session opened -> %s", c->spiffs_path);
    return OTA_FS_OK;
}

static ota_fs_err_t _dt_fclose(void *ctx)
{
    ota_esp32_dt_ctx_t *c = (ota_esp32_dt_ctx_t *)ctx;
    if (!c || !c->is_open) return OTA_FS_ERR_NOT_OPEN;

    c->is_open = false;
    int32_t rc = app_dt_image_write_commit();
    if (rc != APP_DT_IMAGE_OK) {
        LOG_MSG_ERROR(LOG_EN, "fclose: %s not saved (%ld)", c->spiffs_path, (long)rc);
        return OTA_FS_ERR_WRITE_FAIL;
    }
    LOG_MSG_INFO(LOG_EN, "fclose: dispTap file saved -> %s", c->spiffs_path);
    return OTA_FS_OK;
}

static ota_fs_err_t _dt_fwrite(void *ctx, const uint8_t *data, uint32_t len)
{
    ota_esp32_dt_ctx_t *c = (ota_esp32_dt_ctx_t *)ctx;
    if (!c || !c->is_open) return OTA_FS_ERR_NOT_OPEN;
    if (!data || len == 0)  return OTA_FS_ERR_INVALID_ARG;

    int32_t ret = app_dt_image_write(data, (size_t)len);
    if (ret != APP_DT_IMAGE_OK) {
        LOG_MSG_ERROR(LOG_EN, "fwrite: app_dt_image_write failed (%ld)", (long)ret);
        return OTA_FS_ERR_WRITE_FAIL;
    }
    return OTA_FS_OK;
}

static ota_fs_err_t _dt_fread(void *ctx, uint8_t *buf, uint32_t len, uint32_t *out_len)
{
    (void)ctx;
    (void)buf;
    (void)len;
    (void)out_len;
    return OTA_FS_ERR_INVALID_ARG;
}

static ota_fs_err_t _dt_ferase(void *ctx)
{
    ota_esp32_dt_ctx_t *c = (ota_esp32_dt_ctx_t *)ctx;
    if (!c) return OTA_FS_ERR_INVALID_ARG;

    if (c->is_open) {
        /* Remove partial file so a corrupt binary is never used */
        app_dt_image_write_abort();
        c->is_open = false;
        LOG_MSG_INFO(LOG_EN, "ferase: removed partial file %s", c->spiffs_path);
    }
    return OTA_FS_OK;
}

/* -------------------------------------------------------------------------
 * Public driver table  (shared by all dispTap targets via different ctx)
 * ------------------------------------------------------------------------- */

const ota_fs_driver_t g_ota_driver_esp32_dt = {
    .fopen   = _dt_fopen,
    .fclose  = _dt_fclose,
    .fwrite  = _dt_fwrite,
    .fappend = _dt_fwrite,  /* append = write for sequential streaming */
    .fread   = _dt_fread,
    .ferase  = _dt_ferase,
};
