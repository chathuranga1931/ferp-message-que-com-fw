// app_nvs.cpp
//
// Application NVS peripheral — init-guarded wrapper over pal_nvs.
//
// No mutex is held: the ESP-IDF NVS library is internally thread-safe, and
// the Mac simulator runs single-threaded.  This layer exists solely for the
// init guard and consistent logging.

#include "app_nvs.h"
#include "pal_nvs.h"
#include "pal_logger.h"

#define __TAG__  "APP_NVS_"

#ifndef APP_NVS_LOG_EN
#define APP_NVS_LOG_EN true
#endif

// ── Private state ─────────────────────────────────────────────────────────────

static bool s_initialized = false;

// ── Lifecycle ─────────────────────────────────────────────────────────────────

int32_t app_nvs_init(void)
{
    if (s_initialized) return APP_NVS_OK;

    int32_t rc = pal_nvs_init();
    if (rc != PAL_OK) {
        LOG_MSG_ERROR(APP_NVS_LOG_EN, "pal_nvs_init failed (%ld)", (long)rc);
        return APP_NVS_ERR_IO;
    }

    s_initialized = true;
    LOG_MSG_INFO(APP_NVS_LOG_EN, "NVS ready");
    return APP_NVS_OK;
}

// ── Integer (int64) ───────────────────────────────────────────────────────────

int32_t app_nvs_write_i64(const char *ns, const char *key, int64_t value)
{
    if (!s_initialized)  return APP_NVS_ERR_NOT_INIT;
    if (!ns || !key)     return APP_NVS_ERR_INVALID;

    int32_t rc = pal_nvs_write_i64(ns, key, value);
    if (rc != PAL_OK) {
        LOG_MSG_ERROR(APP_NVS_LOG_EN, "write_i64 [%s/%s] failed (%ld)", ns, key, (long)rc);
        return APP_NVS_ERR_IO;
    }
    LOG_MSG_DEBUG(APP_NVS_LOG_EN, "write_i64 [%s/%s] = %lld", ns, key, (long long)value);
    return APP_NVS_OK;
}

int32_t app_nvs_read_i64(const char *ns, const char *key, int64_t *value)
{
    if (!s_initialized)        return APP_NVS_ERR_NOT_INIT;
    if (!ns || !key || !value) return APP_NVS_ERR_INVALID;

    int32_t rc = pal_nvs_read_i64(ns, key, value);
    if (rc == PAL_ERROR_NOT_FOUND) {
        LOG_MSG_DEBUG(APP_NVS_LOG_EN, "read_i64 [%s/%s] not found", ns, key);
        return APP_NVS_ERR_NOT_FOUND;
    }
    if (rc != PAL_OK) {
        LOG_MSG_ERROR(APP_NVS_LOG_EN, "read_i64 [%s/%s] failed (%ld)", ns, key, (long)rc);
        return APP_NVS_ERR_IO;
    }
    LOG_MSG_DEBUG(APP_NVS_LOG_EN, "read_i64 [%s/%s] = %lld", ns, key, (long long)*value);
    return APP_NVS_OK;
}

// ── Blob ──────────────────────────────────────────────────────────────────────

int32_t app_nvs_write_blob(const char *ns, const char *key,
                            const void *data, size_t size)
{
    if (!s_initialized)          return APP_NVS_ERR_NOT_INIT;
    if (!ns || !key || !data || size == 0) return APP_NVS_ERR_INVALID;

    int32_t rc = pal_nvs_write_blob(ns, key, data, size);
    if (rc != PAL_OK) {
        LOG_MSG_ERROR(APP_NVS_LOG_EN, "write_blob [%s/%s] size=%zu failed (%ld)",
                      ns, key, size, (long)rc);
        return APP_NVS_ERR_IO;
    }
    LOG_MSG_DEBUG(APP_NVS_LOG_EN, "write_blob [%s/%s] size=%zu", ns, key, size);
    return APP_NVS_OK;
}

int32_t app_nvs_read_blob(const char *ns, const char *key,
                           void *buf, size_t max_size, size_t *bytes_read)
{
    if (!s_initialized)             return APP_NVS_ERR_NOT_INIT;
    if (!ns || !key || !buf || max_size == 0) return APP_NVS_ERR_INVALID;

    int32_t rc = pal_nvs_read_blob(ns, key, buf, max_size, bytes_read);
    if (rc == PAL_ERROR_NOT_FOUND)  return APP_NVS_ERR_NOT_FOUND;
    if (rc == PAL_ERROR_OVERFLOW)   return APP_NVS_ERR_OVERFLOW;
    if (rc != PAL_OK) {
        LOG_MSG_ERROR(APP_NVS_LOG_EN, "read_blob [%s/%s] failed (%ld)", ns, key, (long)rc);
        return APP_NVS_ERR_IO;
    }
    LOG_MSG_DEBUG(APP_NVS_LOG_EN, "read_blob [%s/%s] size=%zu",
                  ns, key, bytes_read ? *bytes_read : 0);
    return APP_NVS_OK;
}
