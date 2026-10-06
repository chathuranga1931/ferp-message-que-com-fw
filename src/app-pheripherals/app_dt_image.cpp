// app_dt_image.cpp
//
// DispTap firmware image store on the SD card.  See app_dt_image.h.

#include "app_dt_image.h"
#include "app_sd.h"
#include "hsys_mutex.h"
#include "pal_logger.h"
#include "pal_time.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define __TAG__  "DT_IMG  "

#ifndef APP_DT_IMAGE_LOG_EN
#define APP_DT_IMAGE_LOG_EN true
#endif

#define IO_TIMEOUT_MS        3000     // per app_sd call
#define OPEN_LOCK_MS        60000     // flasher waits for a running commit
#define COMMIT_LOCK_MS      20000     // commit waits for a running flash of the images
#define CHUNK                1024

static hsys_mutex_handle_t s_lock = nullptr;
static uint8_t             s_buf[CHUNK];   // verify buffer — used only under s_lock

// OTA write session (owned by the OTA task)
static struct {
    bool     active;
    char     name[32];
    size_t   written;
    uint32_t crc;
    uint32_t min_headroom;   // lowest stack headroom seen in the writing task
} s_wr;

// Flash read session (between open and close)
static bool s_rd_open = false;

// ── Helpers ──────────────────────────────────────────────────────────────────

static uint32_t _crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/** "esp32/distap_esp32.bin" → "distap_esp32.bin" */
static const char *_base(const char *name)
{
    const char *s = strrchr(name, '/');
    return s ? s + 1 : name;
}

static void _sd_path(char *out, size_t n, const char *name, bool tmp)
{
    snprintf(out, n, "%s/%s%s", APP_DT_IMAGE_SD_DIR, _base(name), tmp ? ".tmp" : "");
}

static bool _lock(uint32_t ms)  { return s_lock && hsys_mutex_try_lock(s_lock, ms) > 0; }
static void _unlock(void)       { if (s_lock) hsys_mutex_unlock(s_lock); }

/** Read an SD file back and check its size and CRC32.  Caller holds s_lock. */
static bool _sd_verify(const char *path, size_t expect_size, uint32_t expect_crc)
{
    size_t size = 0;
    if (app_sd_get_file_size(path, &size, IO_TIMEOUT_MS) != APP_SD_OK || size != expect_size) {
        LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "verify %s: size %u, expected %u",
                      path, (unsigned)size, (unsigned)expect_size);
        return false;
    }
    uint32_t crc = 0;
    for (size_t off = 0; off < size; ) {
        size_t n = 0;
        if (app_sd_read_at(path, off, s_buf, sizeof(s_buf), &n, IO_TIMEOUT_MS) != APP_SD_OK || n == 0) {
            LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "verify %s: read failed @%u", path, (unsigned)off);
            return false;
        }
        crc = _crc32(crc, s_buf, n);
        off += n;
    }
    if (crc != expect_crc) {
        LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "verify %s: CRC 0x%08lX, expected 0x%08lX",
                      path, (unsigned long)crc, (unsigned long)expect_crc);
        return false;
    }
    return true;
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void app_dt_image_init(void)
{
    if (!s_lock) s_lock = hsys_mutex_create();
}

// ── OTA write ────────────────────────────────────────────────────────────────

int32_t app_dt_image_write_begin(const char *name)
{
    if (!name || !*name || strlen(_base(name)) >= sizeof(s_wr.name)) return APP_DT_IMAGE_ERR_INVALID;
    if (!app_sd_is_ready()) {
        LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "begin %s: no SD card — DT firmware OTA not supported", _base(name));
        return APP_DT_IMAGE_ERR_NO_SD;
    }
    if (s_wr.active) {
        LOG_MSG_WARNING(APP_DT_IMAGE_LOG_EN, "begin %s: discarding unfinished %s", _base(name), s_wr.name);
        app_dt_image_write_abort();
    }

    memset(&s_wr, 0, sizeof(s_wr));
    strncpy(s_wr.name, _base(name), sizeof(s_wr.name) - 1);

    // Create the folder and an empty "<name>.tmp" here (the OTA source's
    // control task) so every chunk append — which may run on a small writer
    // task — takes the short "file exists" path (no directory walk).
    char tmp[64];
    _sd_path(tmp, sizeof(tmp), s_wr.name, true);
    (void)app_sd_create_dir(APP_DT_IMAGE_SD_DIR, IO_TIMEOUT_MS);   // fails harmlessly if present
    if (app_sd_create_file(tmp, IO_TIMEOUT_MS) != APP_SD_OK) {       // creates or truncates
        LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "begin %s: cannot create", tmp);
        return APP_DT_IMAGE_ERR_IO;
    }

    s_wr.active = true;
    LOG_MSG_INFO(APP_DT_IMAGE_LOG_EN, "begin %s", tmp);
    return APP_DT_IMAGE_OK;
}

int32_t app_dt_image_write(const uint8_t *data, size_t len)
{
    if (!s_wr.active)      return APP_DT_IMAGE_ERR_INVALID;
    if (!data || len == 0) return APP_DT_IMAGE_ERR_INVALID;

    char tmp[64];
    _sd_path(tmp, sizeof(tmp), s_wr.name, true);
    if (app_sd_append_bin(tmp, data, len, IO_TIMEOUT_MS) != APP_SD_OK) {
        LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "write %s failed @%u", tmp, (unsigned)s_wr.written);
        return APP_DT_IMAGE_ERR_IO;
    }
    s_wr.crc = _crc32(s_wr.crc, data, len);
    s_wr.written += len;
#ifdef ESP_PLATFORM
    // SD writes run on the OTA source's writer task — track its stack headroom
    uint32_t hw = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    if (s_wr.min_headroom == 0 || hw < s_wr.min_headroom) {
        if (hw < 512) {
            LOG_MSG_WARNING(APP_DT_IMAGE_LOG_EN, "write %s: task '%s' stack headroom only %u B",
                            s_wr.name, pcTaskGetName(NULL), (unsigned)hw);
        }
        s_wr.min_headroom = hw;
    }
#endif
    return APP_DT_IMAGE_OK;
}

int32_t app_dt_image_write_commit(void)
{
    if (!s_wr.active) return APP_DT_IMAGE_ERR_INVALID;

    char tmp[64], fin[64];
    _sd_path(tmp, sizeof(tmp), s_wr.name, true);
    _sd_path(fin, sizeof(fin), s_wr.name, false);

    if (!_lock(COMMIT_LOCK_MS)) {
        LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "commit %s: image busy (flashing?)", s_wr.name);
        app_dt_image_write_abort();
        return APP_DT_IMAGE_ERR_BUSY;
    }
    int32_t rc = APP_DT_IMAGE_OK;
    if (!_sd_verify(tmp, s_wr.written, s_wr.crc)) {
        rc = APP_DT_IMAGE_ERR_VERIFY;
    } else if (app_sd_rename(tmp, fin, IO_TIMEOUT_MS) != APP_SD_OK) {
        rc = APP_DT_IMAGE_ERR_IO;
    }
    _unlock();

    if (rc != APP_DT_IMAGE_OK) {
        LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "commit %s failed (%ld)", fin, (long)rc);
        app_dt_image_write_abort();
        return rc;
    }
    LOG_MSG_INFO(APP_DT_IMAGE_LOG_EN, "commit %s: %u B CRC 0x%08lX (writer stack headroom min %u B)",
                 fin, (unsigned)s_wr.written, (unsigned long)s_wr.crc, (unsigned)s_wr.min_headroom);
    s_wr.active = false;
    return APP_DT_IMAGE_OK;
}

void app_dt_image_write_abort(void)
{
    if (!s_wr.active) return;
    char tmp[64];
    _sd_path(tmp, sizeof(tmp), s_wr.name, true);
    app_sd_delete_file(tmp, IO_TIMEOUT_MS);
    LOG_MSG_INFO(APP_DT_IMAGE_LOG_EN, "aborted %s", s_wr.name);
    s_wr.active = false;
}

// ── Flash read ───────────────────────────────────────────────────────────────

int32_t app_dt_image_open(const char *name, size_t *size)
{
    if (!name || !size) return APP_DT_IMAGE_ERR_INVALID;
    *size = 0;
    if (!app_sd_is_ready()) {
        LOG_MSG_WARNING(APP_DT_IMAGE_LOG_EN, "open %s: no SD card", _base(name));
        return APP_DT_IMAGE_ERR_NO_SD;
    }
    if (!_lock(OPEN_LOCK_MS)) {
        LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "open %s: image lock busy", _base(name));
        return APP_DT_IMAGE_ERR_BUSY;
    }

    char path[64];
    _sd_path(path, sizeof(path), name, false);
    if (app_sd_get_file_size(path, size, IO_TIMEOUT_MS) != APP_SD_OK || *size == 0) {
        _unlock();
        LOG_MSG_INFO(APP_DT_IMAGE_LOG_EN, "open %s: no image on SD", path);
        return APP_DT_IMAGE_ERR_NOT_FOUND;
    }
    s_rd_open = true;
    return APP_DT_IMAGE_OK;
}

int32_t app_dt_image_read_at(const char *name, size_t offset, uint8_t *buf,
                             size_t len, size_t *bytes_read)
{
    if (!name || !buf || !s_rd_open) return APP_DT_IMAGE_ERR_INVALID;

    char path[64];
    _sd_path(path, sizeof(path), name, false);

    // A busy card (SD log write in progress) is retried rather than failing the flash
    for (int attempt = 0; attempt < 3; attempt++) {
        size_t n = 0;
        if (app_sd_read_at(path, offset, buf, len, &n, IO_TIMEOUT_MS) == APP_SD_OK) {
            if (bytes_read) *bytes_read = n;
            return APP_DT_IMAGE_OK;
        }
        pal_time_delay_ms(50);
    }
    LOG_MSG_ERROR(APP_DT_IMAGE_LOG_EN, "read %s @%u failed", path, (unsigned)offset);
    return APP_DT_IMAGE_ERR_IO;
}

void app_dt_image_close(const char *name)
{
    if (!s_rd_open) return;
    s_rd_open = false;
    _unlock();
#ifdef ESP_PLATFORM
    // The flasher reads from the SD card on the caller's task — report its headroom
    LOG_MSG_INFO(APP_DT_IMAGE_LOG_EN, "close %s: task '%s' stack headroom %u B",
                 _base(name), pcTaskGetName(NULL), (unsigned)uxTaskGetStackHighWaterMark(NULL));
#else
    (void)name;
#endif
}
