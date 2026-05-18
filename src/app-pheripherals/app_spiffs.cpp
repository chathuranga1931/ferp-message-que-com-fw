// app_spiffs.cpp
//
// Application SPIFFS peripheral — mutex-protected wrapper over pal_spiffs.
//
// All public functions follow the same pattern:
//   1. Guard: check initialized
//   2. Acquire mutex (with timeout)
//   3. Delegate to pal_spiffs
//   4. Release mutex
//   5. Return app-level error code

#include "app_spiffs.h"
#include "pal_spiffs.h"
#include "pal_logger.h"
#include "hsys_mutex.h"

#include <string.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/stat.h>

#define __TAG__  "SPIFFS_P"

#ifndef APP_SPIFFS_LOG_EN
#define APP_SPIFFS_LOG_EN true
#endif

// ── Private state ─────────────────────────────────────────────────────────────

static bool                  s_initialized = false;
static hsys_mutex_handle_t   s_mutex       = nullptr;

// ── Internal helpers ──────────────────────────────────────────────────────────

static int32_t _lock(uint32_t timeout_ms)
{
    if (!s_mutex) return APP_SPIFFS_ERR_NOT_INIT;
    int32_t r = hsys_mutex_try_lock(s_mutex, timeout_ms);
    return (r > 0) ? APP_SPIFFS_OK : APP_SPIFFS_ERR_BUSY;
}

static void _unlock(void)
{
    if (s_mutex) hsys_mutex_unlock(s_mutex);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

int32_t app_spiffs_init(void)
{
    if (s_initialized) return APP_SPIFFS_OK;

    s_mutex = hsys_mutex_create();
    if (!s_mutex) {
        LOG_MSG_ERROR(APP_SPIFFS_LOG_EN, "mutex create failed");
        return APP_SPIFFS_ERR_IO;
    }

    pal_spiffs_config_t cfg = {
        .base_path           = "/spiffs",
        .partition_label     = NULL,
        .max_files           = 5,
        .format_if_mount_failed = true
    };

    pal_spiffs_info_t info = {};
    int32_t rc = pal_spiffs_init(&cfg, &info);
    if (rc != PAL_OK) {
        LOG_MSG_ERROR(APP_SPIFFS_LOG_EN, "pal_spiffs_init failed (%ld)", (long)rc);
        return APP_SPIFFS_ERR_IO;
    }

    s_initialized = true;
    LOG_MSG_INFO(APP_SPIFFS_LOG_EN, "ready — total=%zu used=%zu free=%zu",
                 info.total_bytes, info.used_bytes, info.free_bytes);
    return APP_SPIFFS_OK;
}

// ── File I/O ──────────────────────────────────────────────────────────────────

int32_t app_spiffs_write_file(const char *path, const char *content, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SPIFFS_ERR_NOT_INIT;
    if (!path || !content)  return APP_SPIFFS_ERR_INVALID;

    int32_t rc = _lock(timeout_ms);
    if (rc != APP_SPIFFS_OK) return rc;

    size_t len = strlen(content);
    int32_t pal = pal_spiffs_file_write(path, (const uint8_t *)content, len);

    _unlock();

    if (pal != PAL_OK) {
        LOG_MSG_ERROR(APP_SPIFFS_LOG_EN, "write failed: %s (%ld)", path, (long)pal);
        return APP_SPIFFS_ERR_IO;
    }
    LOG_MSG_DEBUG(APP_SPIFFS_LOG_EN, "wrote %zu bytes → %s", len, path);
    return APP_SPIFFS_OK;
}

int32_t app_spiffs_read_file(const char *path, char *buffer, size_t buf_size,
                             size_t *bytes_read, uint32_t timeout_ms)
{
    if (!s_initialized)       return APP_SPIFFS_ERR_NOT_INIT;
    if (!path || !buffer || buf_size == 0) return APP_SPIFFS_ERR_INVALID;

    int32_t rc = _lock(timeout_ms);
    if (rc != APP_SPIFFS_OK) return rc;

    // Check existence first so we give a clear error
    bool exists = false;
    pal_spiffs_file_exists(path, &exists);
    if (!exists) {
        _unlock();
        return APP_SPIFFS_ERR_NOT_FOUND;
    }

    size_t n = 0;
    int32_t pal = pal_spiffs_file_read(path, (uint8_t *)buffer, buf_size - 1, &n);
    _unlock();

    if (pal != PAL_OK) {
        LOG_MSG_ERROR(APP_SPIFFS_LOG_EN, "read failed: %s (%ld)", path, (long)pal);
        return APP_SPIFFS_ERR_IO;
    }

    buffer[n] = '\0';   // ensure NUL-termination
    if (bytes_read) *bytes_read = n;
    LOG_MSG_DEBUG(APP_SPIFFS_LOG_EN, "read %zu bytes ← %s", n, path);
    return APP_SPIFFS_OK;
}

int32_t app_spiffs_read_file_at(const char *path, size_t offset, char *buffer,
                                size_t buf_size, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!s_initialized)        return APP_SPIFFS_ERR_NOT_INIT;
    if (!path || !buffer || buf_size == 0) return APP_SPIFFS_ERR_INVALID;

    int32_t rc = _lock(timeout_ms);
    if (rc != APP_SPIFFS_OK) return rc;

    bool exists = false;
    pal_spiffs_file_exists(path, &exists);
    if (!exists) { _unlock(); return APP_SPIFFS_ERR_NOT_FOUND; }

    size_t n = 0;
    int32_t pal = pal_spiffs_file_read_at(path, offset, (uint8_t *)buffer, buf_size, &n);
    _unlock();

    if (pal != PAL_OK) { return APP_SPIFFS_ERR_IO; }
    if (bytes_read) *bytes_read = n;
    return APP_SPIFFS_OK;
}

int32_t app_spiffs_file_exists(const char *path, bool *exists, uint32_t timeout_ms)
{
    if (!s_initialized)   return APP_SPIFFS_ERR_NOT_INIT;
    if (!path || !exists) return APP_SPIFFS_ERR_INVALID;

    int32_t rc = _lock(timeout_ms);
    if (rc != APP_SPIFFS_OK) return rc;

    int32_t pal = pal_spiffs_file_exists(path, exists);
    _unlock();

    return (pal == PAL_OK) ? APP_SPIFFS_OK : APP_SPIFFS_ERR_IO;
}

int32_t app_spiffs_delete_file(const char *path, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SPIFFS_ERR_NOT_INIT;
    if (!path)          return APP_SPIFFS_ERR_INVALID;

    int32_t rc = _lock(timeout_ms);
    if (rc != APP_SPIFFS_OK) return rc;

    bool exists = false;
    pal_spiffs_file_exists(path, &exists);
    int32_t pal = PAL_OK;
    if (exists) {
        pal = pal_spiffs_file_delete(path);
    }
    _unlock();

    if (!exists)        return APP_SPIFFS_ERR_NOT_FOUND;
    if (pal != PAL_OK)  return APP_SPIFFS_ERR_IO;
    LOG_MSG_DEBUG(APP_SPIFFS_LOG_EN, "deleted %s", path);
    return APP_SPIFFS_OK;
}

int32_t app_spiffs_append_file(const char *path, const uint8_t *data, size_t size,
                               uint32_t timeout_ms)
{
    if (!s_initialized)         return APP_SPIFFS_ERR_NOT_INIT;
    if (!path || !data || !size) return APP_SPIFFS_ERR_INVALID;

    int32_t rc = _lock(timeout_ms);
    if (rc != APP_SPIFFS_OK) return rc;

    int32_t pal = pal_spiffs_file_append(path, data, size);
    _unlock();

    if (pal != PAL_OK) {
        LOG_MSG_ERROR(APP_SPIFFS_LOG_EN, "append failed: %s (%ld)", path, (long)pal);
        return APP_SPIFFS_ERR_IO;
    }
    return APP_SPIFFS_OK;
}

int32_t app_spiffs_get_info(app_spiffs_info_t *info)
{
    if (!info) return APP_SPIFFS_ERR_INVALID;

    pal_spiffs_info_t pi = {};
    int32_t pal = pal_spiffs_get_info(&pi);
    if (pal != PAL_OK) return APP_SPIFFS_ERR_IO;

    info->total_bytes = pi.total_bytes;
    info->used_bytes  = pi.used_bytes;
    info->free_bytes  = pi.free_bytes;
    return APP_SPIFFS_OK;
}

int32_t app_spiffs_list_files(app_spiffs_file_cb_t cb, void *ctx, uint32_t timeout_ms)
{
    if (!s_initialized) return APP_SPIFFS_ERR_NOT_INIT;
    if (!cb)            return APP_SPIFFS_ERR_INVALID;

    int32_t rc = _lock(timeout_ms);
    if (rc != APP_SPIFFS_OK) return rc;

    DIR *dir = opendir("/spiffs");
    if (!dir) {
        _unlock();
        LOG_MSG_ERROR(APP_SPIFFS_LOG_EN, "list_files: opendir failed");
        return APP_SPIFFS_ERR_IO;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Skip macOS Apple Double resource-fork sidecars.
        if (entry->d_name[0] == '.' && entry->d_name[1] == '_') continue;
        // Skip non-regular entries on targets that populate d_type.
        if (entry->d_type != DT_REG) continue;

        char full_path[270];  // "/spiffs/" (8) + NAME_MAX (255) + NUL
        snprintf(full_path, sizeof(full_path), "/spiffs/%s", entry->d_name);
        struct stat st;
        size_t file_size = (stat(full_path, &st) == 0) ? (size_t)st.st_size : 0;

        cb(entry->d_name, file_size, ctx);
    }
    closedir(dir);
    _unlock();
    return APP_SPIFFS_OK;
}
