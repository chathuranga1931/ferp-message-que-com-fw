/**
 * pal_mac_fw_update.cpp
 *
 * macOS/Linux simulator implementation of pal_fw_update.h.
 *
 * The firmware binary is streamed into the SPIFFS emulation directory so it
 * appears in the sim-ui SPIFFS file browser exactly as it would appear on the
 * device.  A timestamped filename is used so successive OTA runs are preserved:
 *
 *   <cwd>/SPIFFS/spiffs/fw_<ddmmyyyy_hhmmss>.bin
 *
 * Example:  SPIFFS/spiffs/fw_27042026_143022.bin
 *
 * No actual firmware is installed — this is a simulator stub that allows the
 * full OTA state machine to run and produce realistic log output without
 * touching any flash partition.
 *
 * On end() success the file is left on disk so the host can inspect it.
 * On abort() the partial file is removed.
 *
 * Session model mirrors pal_esp_idf_fw_update.cpp:
 *   only one session may be active at a time (enforced by begin()).
 *   The handle is a pointer to the single static session context.
 */

#include "pal_fw_update.h"
#include "pal_logger.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#define __TAG__  "PAL_FWUP"

#define FWUP_ERROR_LOG_EN  true
#define FWUP_INFO_LOG_EN   true
#define FWUP_DEBUG_LOG_EN  false

/* SPIFFS emulation root relative to cwd — mirrors pal_mac_spiffs.cpp */
#define SPIFFS_OTA_DIR  "SPIFFS/spiffs"

/*===========================================================================*/
/*                         INTERNAL SESSION STATE                            */
/*===========================================================================*/

typedef struct {
    FILE                  *file;
    bool                   is_open;
    char                   path[256];   /* resolved host path of current session */
    pal_fw_update_status_t status;
} fw_update_ctx_t;

static fw_update_ctx_t s_ctx = {
    .file    = NULL,
    .is_open = false,
    .path    = {0},
    .status  = {
        .state         = PAL_FW_UPDATE_STATE_IDLE,
        .bytes_written = 0,
        .error_code    = PAL_OK,
        .status_str    = "Idle",
    },
};

/*===========================================================================*/
/*                         INTERNAL HELPERS                                  */
/*===========================================================================*/

static void _set_status(pal_fw_update_state_t state, int32_t err, const char *fmt, ...)
{
    s_ctx.status.state      = state;
    s_ctx.status.error_code = err;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_ctx.status.status_str, sizeof(s_ctx.status.status_str), fmt, ap);
    va_end(ap);
}

/** Ensure a directory path exists (like mkdir -p). */
static void _mkdirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

/*===========================================================================*/
/*                         API IMPLEMENTATION                                */
/*===========================================================================*/

int32_t pal_fw_update_begin(pal_fw_update_handle_t *handle)
{
    if (!handle) return PAL_ERROR_INVALID;
    *handle = NULL;

    if (s_ctx.is_open) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "OTA session already in progress");
        return PAL_ERROR_BUSY;
    }

    /* Build timestamped filename: fw_<ddmmyyyy_hhmmss>.bin */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char filename[64];
    snprintf(filename, sizeof(filename),
             "fw_%02d%02d%04d_%02d%02d%02d.bin",
             t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
             t->tm_hour, t->tm_min, t->tm_sec);

    /* Ensure SPIFFS OTA directory exists and build full path */
    _mkdirs(SPIFFS_OTA_DIR);
    snprintf(s_ctx.path, sizeof(s_ctx.path), "%s/%s", SPIFFS_OTA_DIR, filename);

    s_ctx.file = fopen(s_ctx.path, "wb");
    if (!s_ctx.file) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "fopen(%s) failed: %s", s_ctx.path, strerror(errno));
        _set_status(PAL_FW_UPDATE_STATE_FAILED, PAL_ERROR_IO, "Cannot open output file");
        return PAL_ERROR_IO;
    }

    s_ctx.is_open              = true;
    s_ctx.status.bytes_written = 0;
    _set_status(PAL_FW_UPDATE_STATE_IN_PROGRESS, PAL_OK, "In progress");

    LOG_MSG_INFO(FWUP_INFO_LOG_EN, "OTA session started → %s", s_ctx.path);
    *handle = (pal_fw_update_handle_t)&s_ctx;
    return PAL_OK;
}

int32_t pal_fw_update_write(pal_fw_update_handle_t handle,
                            const uint8_t         *data,
                            size_t                 len)
{
    if (!handle || !data || len == 0) return PAL_ERROR_INVALID;

    fw_update_ctx_t *c = (fw_update_ctx_t *)handle;
    if (!c->is_open || !c->file) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "write called with no active session");
        return PAL_ERROR_INVALID;
    }

    size_t written = fwrite(data, 1, len, c->file);
    if (written != len) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "fwrite: wrote %zu / %zu bytes", written, len);
        _set_status(PAL_FW_UPDATE_STATE_FAILED, PAL_ERROR_IO, "Write failed");
        return PAL_ERROR_IO;
    }

    /* Flush after each chunk so the file is inspectable mid-download */
    fflush(c->file);

    c->status.bytes_written += (uint32_t)len;
    _set_status(PAL_FW_UPDATE_STATE_IN_PROGRESS, PAL_OK,
                "In progress: %lu bytes", (unsigned long)c->status.bytes_written);
    return PAL_OK;
}

int32_t pal_fw_update_end(pal_fw_update_handle_t handle)
{
    if (!handle) return PAL_ERROR_INVALID;

    fw_update_ctx_t *c = (fw_update_ctx_t *)handle;
    if (!c->is_open) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN, "end called with no active session");
        return PAL_ERROR_INVALID;
    }

    fclose(c->file);
    c->file    = NULL;
    c->is_open = false;

    LOG_MSG_INFO(FWUP_INFO_LOG_EN,
                 "OTA complete: %lu bytes written to %s",
                 (unsigned long)c->status.bytes_written, c->path);
    _set_status(PAL_FW_UPDATE_STATE_SUCCESS, PAL_OK,
                "Success: %lu bytes", (unsigned long)c->status.bytes_written);
    return PAL_OK;
}

int32_t pal_fw_update_abort(pal_fw_update_handle_t handle)
{
    if (!handle) return PAL_ERROR_INVALID;

    fw_update_ctx_t *c = (fw_update_ctx_t *)handle;
    if (!c->is_open) return PAL_OK;   /* already closed — nothing to do */

    fclose(c->file);
    c->file    = NULL;
    c->is_open = false;

    if (remove(c->path) != 0) {
        LOG_MSG_ERROR(FWUP_ERROR_LOG_EN,
                      "remove(%s) failed (file may not exist)", c->path);
    }

    LOG_MSG_INFO(FWUP_INFO_LOG_EN, "OTA session aborted — partial file removed (%s)", c->path);
    _set_status(PAL_FW_UPDATE_STATE_ABORTED, PAL_OK, "Aborted");
    return PAL_OK;
}

int32_t pal_fw_update_get_status(pal_fw_update_status_t *status)
{
    if (!status) return PAL_ERROR_INVALID;
    *status = s_ctx.status;
    return PAL_OK;
}

