/**
 * @file ota_driver_esp32_dt.c
 * @brief OTA filesystem driver for ESP32 dispTap (esp07) binary files.
 *
 * Uses PAL SPIFFS APIs to write each received binary to a fixed path on
 * SPIFFS.  The file is always overwritten — no timestamps.
 *
 *   fopen  → pal_spiffs_file_delete() clears any stale file, sets is_open
 *   fwrite → pal_spiffs_file_append() streams each incoming chunk
 *   fclose → clears is_open (data committed by each append call)
 *   ferase → pal_spiffs_file_delete() removes partial file on abort
 *   fread  → not supported
 *
 * Path examples (relative to SPIFFS mount — no leading '/'):
 *   "esp32/bootloader.bin"  "esp32/partitions.bin"  "esp32/firmware.bin"
 *
 * On simulator: resolves to <cwd>/SPIFFS/spiffs/<spiffs_path>
 * On ESP32 VFS: resolves to /spiffs/<spiffs_path>
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_driver_esp32_dt.h"
#include "pal_spiffs.h"
#include "pal_logger.h"

#define __TAG__  "OTA_DT  "
#define LOG_EN   true

/* PAL return codes */
#define PAL_OK  0

/* -------------------------------------------------------------------------
 * Driver function implementations
 * ------------------------------------------------------------------------- */

static ota_fs_err_t _dt_fopen(void *ctx, const char *path, ota_fs_open_mode_t mode)
{
    (void)path;   /* path comes from ctx->spiffs_path */
    (void)mode;   /* always write mode */

    ota_esp32_dt_ctx_t *c = (ota_esp32_dt_ctx_t *)ctx;
    if (!c || !c->spiffs_path) return OTA_FS_ERR_INVALID_ARG;

    /* Delete any stale file so we start with a clean slate */
    pal_spiffs_file_delete(c->spiffs_path);   /* ignore error if doesn't exist */

    c->is_open = true;
    LOG_MSG_INFO(LOG_EN, "fopen: dispTap OTA session opened -> %s", c->spiffs_path);
    return OTA_FS_OK;
}

static ota_fs_err_t _dt_fclose(void *ctx)
{
    ota_esp32_dt_ctx_t *c = (ota_esp32_dt_ctx_t *)ctx;
    if (!c || !c->is_open) return OTA_FS_ERR_NOT_OPEN;

    c->is_open = false;
    LOG_MSG_INFO(LOG_EN, "fclose: dispTap file saved -> %s", c->spiffs_path);
    return OTA_FS_OK;
}

static ota_fs_err_t _dt_fwrite(void *ctx, const uint8_t *data, uint32_t len)
{
    ota_esp32_dt_ctx_t *c = (ota_esp32_dt_ctx_t *)ctx;
    if (!c || !c->is_open) return OTA_FS_ERR_NOT_OPEN;
    if (!data || len == 0)  return OTA_FS_ERR_INVALID_ARG;

    int32_t ret = pal_spiffs_file_append(c->spiffs_path, data, (size_t)len);
    if (ret != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "fwrite: pal_spiffs_file_append failed (%ld)", (long)ret);
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
        pal_spiffs_file_delete(c->spiffs_path);
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
