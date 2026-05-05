// app_sd.cpp
//
// Application SD-card peripheral — mutex-protected wrapper over pal_sd.
//
// All public functions follow the same pattern:
//   1. Guard: check initialized
//   2. Acquire mutex (with timeout)
//   3. Delegate to pal_sd
//   4. Release mutex
//   5. Return app-level error code

#include "app_sd.h"
#include "pal_sd.h"
#include "pal_logger.h"
#include "hsys_mutex.h"

#include <string.h>
#include <stddef.h>

#define __TAG__  "APP_SD  "

#ifndef APP_SD_LOG_EN
#define APP_SD_LOG_EN true
#endif

// ── Private state ─────────────────────────────────────────────────────────────

static bool                  s_initialized = false;
static hsys_mutex_handle_t   s_mutex       = nullptr;

// ── Internal helpers ──────────────────────────────────────────────────────────

static int32_t _lock(uint32_t timeout_ms)
{
    if (!s_mutex) return APP_SD_ERR_NOT_INIT;
    int32_t r = hsys_mutex_try_lock(s_mutex, timeout_ms);
    return (r > 0) ? APP_SD_OK : APP_SD_ERR_BUSY;
}

static void _unlock(void)
{
    if (s_mutex) hsys_mutex_unlock(s_mutex);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

int32_t app_sd_init(app_sd_info_t *info)
{
    if (s_initialized) return APP_SD_OK;

    s_mutex = hsys_mutex_create();
    if (!s_mutex) {
        LOG_MSG_ERROR(APP_SD_LOG_EN, "mutex create failed");
        return APP_SD_ERR_IO;
    }

    pal_sd_info_t pal_info{};
    int32_t rc = pal_sd_init(nullptr, &pal_info);
    if (rc != PAL_OK) {
        LOG_MSG_ERROR(APP_SD_LOG_EN, "pal_sd_init failed (%ld)", (long)rc);
        return APP_SD_ERR_IO;
    }

    s_initialized = true;

    if (info) {
        info->card_size_mb = pal_info.card_size_mb;
        strncpy(info->card_type, pal_info.card_type, sizeof(info->card_type) - 1);
    }

    LOG_MSG_INFO(APP_SD_LOG_EN, "ready — card=%s %llu MB",
                 pal_info.card_type, (unsigned long long)pal_info.card_size_mb);
    return APP_SD_OK;
}

int32_t app_sd_get_free_mb(uint64_t *free_mb)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!free_mb)        return APP_SD_ERR_INVALID;

    int32_t rc = pal_sd_get_free_mb(free_mb);
    return (rc == PAL_OK) ? APP_SD_OK : APP_SD_ERR_IO;
}

// ── File write ────────────────────────────────────────────────────────────────

int32_t app_sd_write_file(const char *path, const char *content, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path || !content) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    int32_t ret = APP_SD_OK;
    if (pal_sd_file_write(path, content, strlen(content)) != PAL_OK) {
        LOG_MSG_ERROR(APP_SD_LOG_EN, "write_file failed: %s", path);
        ret = APP_SD_ERR_IO;
    }
    _unlock();
    return ret;
}

// ── Append line ───────────────────────────────────────────────────────────────

int32_t app_sd_append_line(const char *path, const char *line, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path || !line) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    char buf[512];
    snprintf(buf, sizeof(buf), "%s\n", line);

    int32_t ret = APP_SD_OK;
    if (pal_sd_file_append(path, buf, strlen(buf)) != PAL_OK) {
        LOG_MSG_ERROR(APP_SD_LOG_EN, "append_line failed: %s", path);
        ret = APP_SD_ERR_IO;
    }
    _unlock();
    return ret;
}

// ── Read file ─────────────────────────────────────────────────────────────────

int32_t app_sd_read_file(const char *path, char *buffer, size_t buf_size,
                          size_t *bytes_read, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path || !buffer || buf_size == 0) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    // Check size first
    size_t file_size = 0;
    int32_t ret = APP_SD_OK;
    if (pal_sd_file_get_size(path, &file_size) != PAL_OK) {
        ret = APP_SD_ERR_NOT_FOUND;
    } else if (file_size >= buf_size) {
        LOG_MSG_ERROR(APP_SD_LOG_EN, "read_file: file too large (%zu >= %zu): %s",
                      file_size, buf_size, path);
        ret = APP_SD_ERR_TOO_LARGE;
    } else {
        if (pal_sd_file_read(path, buffer, buf_size, bytes_read) != PAL_OK) {
            LOG_MSG_ERROR(APP_SD_LOG_EN, "read_file failed: %s", path);
            ret = APP_SD_ERR_IO;
        }
    }
    _unlock();
    return ret;
}

// ── Read line ─────────────────────────────────────────────────────────────────

int32_t app_sd_read_line(const char *path, uint32_t line_number,
                          char *buffer, size_t max_len, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path || !buffer) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    int32_t ret = APP_SD_OK;
    if (pal_sd_file_read_line(path, line_number, buffer, max_len) != PAL_OK) {
        ret = APP_SD_ERR_IO;
    }
    _unlock();
    return ret;
}

// ── Create / delete file ──────────────────────────────────────────────────────

int32_t app_sd_create_file(const char *path, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    int32_t ret = (pal_sd_file_create(path) == PAL_OK) ? APP_SD_OK : APP_SD_ERR_IO;
    _unlock();
    return ret;
}

int32_t app_sd_delete_file(const char *path, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    int32_t ret = APP_SD_OK;
    bool exists = false;
    if (pal_sd_file_exists(path, &exists) == PAL_OK && exists) {
        if (pal_sd_file_delete(path) != PAL_OK) {
            LOG_MSG_ERROR(APP_SD_LOG_EN, "delete_file failed: %s", path);
            ret = APP_SD_ERR_IO;
        }
    }
    _unlock();
    return ret;
}

// ── Directory ops ─────────────────────────────────────────────────────────────

int32_t app_sd_create_dir(const char *path, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    int32_t ret = (pal_sd_dir_create(path) == PAL_OK) ? APP_SD_OK : APP_SD_ERR_IO;
    _unlock();
    return ret;
}

int32_t app_sd_remove_dir(const char *path, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    int32_t ret = (pal_sd_dir_remove(path) == PAL_OK) ? APP_SD_OK : APP_SD_ERR_IO;
    if (ret != APP_SD_OK)
        LOG_MSG_ERROR(APP_SD_LOG_EN, "remove_dir failed: %s", path);
    _unlock();
    return ret;
}

int32_t app_sd_dir_open(const char *path, pal_sd_dir_handle_t *handle,
                         uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SD_ERR_NOT_INIT;
    if (!path || !handle) return APP_SD_ERR_INVALID;
    if (_lock(timeout_ms) != APP_SD_OK) return APP_SD_ERR_BUSY;

    int32_t ret = (pal_sd_dir_open(path, handle) == PAL_OK) ? APP_SD_OK : APP_SD_ERR_IO;
    _unlock();
    return ret;
}

int32_t app_sd_dir_read_next(pal_sd_dir_handle_t handle,
                              pal_sd_dir_entry_t *entry, bool *has_more)
{
    // No lock held — caller holds the handle lifecycle
    return pal_sd_dir_read_next(handle, entry, has_more);
}

int32_t app_sd_dir_close(pal_sd_dir_handle_t handle)
{
    return pal_sd_dir_close(handle);
}

// ── storage_interface_t adapter ───────────────────────────────────────────────
//
// Wraps app_sd_* into the generic storage_interface_t used by list_manager
// and retransmission_manager.  The adapter ignores the get_next_file slot
// because list_manager does not use it.

static int32_t _si_delete_file(const char *path, uint32_t timeout_ms)
{
    return app_sd_delete_file(path, timeout_ms);
}

static int32_t _si_create_file(const char *path, uint32_t timeout_ms)
{
    return app_sd_create_file(path, timeout_ms);
}

static int32_t _si_append_line(const char *path, const char *line, uint32_t timeout_ms)
{
    return app_sd_append_line(path, line, timeout_ms);
}

static int32_t _si_write_file(const char *path, const char *content, uint32_t timeout_ms)
{
    return app_sd_write_file(path, content, timeout_ms);
}

static int32_t _si_read_file(const char *path, char *buf, size_t max_len, uint32_t timeout_ms)
{
    size_t bytes_read = 0;
    return app_sd_read_file(path, buf, max_len, &bytes_read, timeout_ms);
}

static int32_t _si_remove_dir(const char *path, uint32_t timeout_ms)
{
    return app_sd_remove_dir(path, timeout_ms);
}

static int32_t _si_read_line(const char *path, uint32_t line_number,
                              char *buf, size_t max_len, uint32_t timeout_ms)
{
    return app_sd_read_line(path, line_number, buf, max_len, timeout_ms);
}

static const storage_interface_t s_sd_storage_iface = {
    .delete_file   = _si_delete_file,
    .create_file   = _si_create_file,
    .append_line   = _si_append_line,
    .write_file    = _si_write_file,
    .read_file     = _si_read_file,
    .remove_dir    = _si_remove_dir,
    .read_line     = _si_read_line,
    .get_next_file = nullptr,
};

const storage_interface_t *app_sd_get_storage_interface(void)
{
    return &s_sd_storage_iface;
}
