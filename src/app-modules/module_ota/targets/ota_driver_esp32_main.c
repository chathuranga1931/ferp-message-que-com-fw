/**
 * @file ota_driver_esp32_main.c
 * @brief OTA filesystem driver for the ESP32 main application firmware slot.
 *
 * Implements ota_fs_driver_t by wrapping pal_fw_update_* calls:
 *
 *   fopen  → pal_fw_update_begin()   (selects next OTA slot; path arg ignored)
 *   fwrite → pal_fw_update_write()   (sequential chunk write)
 *   fappend→ pal_fw_update_write()   (same as fwrite for this target)
 *   fclose → pal_fw_update_end()     (validates image + sets boot partition; no reboot)
 *   ferase → pal_fw_update_abort()   (releases OTA handle; called on abort/timeout)
 *   fread  → not supported           (returns OTA_FS_ERR_INVALID_ARG)
 *
 * The ctx pointer must point to a zero-initialised ota_esp32_ctx_t struct with
 * static lifetime (allocated in main.cpp alongside the driver table).
 *
 * Usage (in main.cpp):
 *   static ota_esp32_ctx_t  s_esp32_ota_ctx = {};
 *   static const ota_fs_driver_t s_esp32_ota_driver = OTA_DRIVER_ESP32_MAIN_INIT;
 *   static const ota_target_desc_t s_ota_targets[] = {
 *       { .target_idx = 0, .needs_reboot = true, .label = "esp32-main",
 *         .driver = &s_esp32_ota_driver, .ctx = &s_esp32_ota_ctx },
 *   };
 */

#include "ota_driver_esp32_main.h"
#include "pal_fw_update.h"
#include "pal_logger.h"
#include <string.h>

#define __TAG__  "OTA_DRV "
#define LOG_EN   true

/* pal_fw_update return codes */
#define PAL_OK  0

/* -------------------------------------------------------------------------
 * Driver function implementations
 * ------------------------------------------------------------------------- */

static ota_fs_err_t _esp32_fopen(void *ctx, const char *path, ota_fs_open_mode_t mode)
{
    (void)path;  /* OTA slot selected automatically by pal_fw_update_begin */
    (void)mode;  /* Always write mode for this target */

    ota_esp32_ctx_t *c = (ota_esp32_ctx_t *)ctx;
    if (!c) return OTA_FS_ERR_INVALID_ARG;

    /* Ensure no stale handle is present */
    c->handle = NULL;

    int32_t ret = pal_fw_update_begin(&c->handle);
    if (ret != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "fopen: pal_fw_update_begin failed (%ld)", (long)ret);
        c->handle = NULL;
        return OTA_FS_ERR_UNKNOWN;
    }

    LOG_MSG_INFO(LOG_EN, "fopen: OTA slot opened");
    return OTA_FS_OK;
}

static ota_fs_err_t _esp32_fclose(void *ctx)
{
    ota_esp32_ctx_t *c = (ota_esp32_ctx_t *)ctx;
    if (!c || !c->handle) return OTA_FS_ERR_NOT_OPEN;

    int32_t ret = pal_fw_update_end(c->handle);
    c->handle = NULL;

    if (ret != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "fclose: pal_fw_update_end failed (%ld) — image invalid?", (long)ret);
        return OTA_FS_ERR_WRITE_FAIL;
    }

    LOG_MSG_INFO(LOG_EN, "fclose: image validated, boot slot committed");
    return OTA_FS_OK;
}

static ota_fs_err_t _esp32_fwrite(void *ctx, const uint8_t *data, uint32_t len)
{
    ota_esp32_ctx_t *c = (ota_esp32_ctx_t *)ctx;
    if (!c || !c->handle) return OTA_FS_ERR_NOT_OPEN;
    if (!data || len == 0) return OTA_FS_ERR_INVALID_ARG;

    int32_t ret = pal_fw_update_write(c->handle, data, (size_t)len);
    if (ret != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "fwrite: pal_fw_update_write failed (%ld)", (long)ret);
        return OTA_FS_ERR_WRITE_FAIL;
    }
    return OTA_FS_OK;
}

static ota_fs_err_t _esp32_fread(void *ctx, uint8_t *buf, uint32_t len, uint32_t *out_len)
{
    (void)ctx;
    (void)buf;
    (void)len;
    (void)out_len;
    /* Read-back of OTA partition is not supported by this driver */
    return OTA_FS_ERR_INVALID_ARG;
}

static ota_fs_err_t _esp32_ferase(void *ctx)
{
    ota_esp32_ctx_t *c = (ota_esp32_ctx_t *)ctx;
    if (!c) return OTA_FS_ERR_INVALID_ARG;

    if (!c->handle) {
        /* Nothing to abort — already clean */
        return OTA_FS_OK;
    }

    int32_t ret = pal_fw_update_abort(c->handle);
    c->handle = NULL;

    if (ret != PAL_OK) {
        LOG_MSG_ERROR(LOG_EN, "ferase: pal_fw_update_abort failed (%ld)", (long)ret);
        return OTA_FS_ERR_ERASE_FAIL;
    }

    LOG_MSG_INFO(LOG_EN, "ferase: OTA handle released");
    return OTA_FS_OK;
}

/* -------------------------------------------------------------------------
 * Public driver table
 * Use OTA_DRIVER_ESP32_MAIN_INIT to initialise a const ota_fs_driver_t.
 * ------------------------------------------------------------------------- */

const ota_fs_driver_t g_ota_driver_esp32_main = {
    .fopen   = _esp32_fopen,
    .fclose  = _esp32_fclose,
    .fwrite  = _esp32_fwrite,
    .fappend = _esp32_fwrite,   /* append = write for sequential flash */
    .fread   = _esp32_fread,
    .ferase  = _esp32_ferase,
};
