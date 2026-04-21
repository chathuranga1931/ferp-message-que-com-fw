/**
 * @file pal_mac_sd.cpp
 * @brief PAL SD card implementation for macOS simulator.
 *
 * Emulates an SD card using a local directory:
 *   src/product/ferp-com-simulator/SDCARD/
 *
 * All paths passed to the public API are relative and are resolved against
 * this root.  Directory entries that begin with '.' are skipped (handles
 * both POSIX "." / ".." and macOS ".DS_Store").
 *
 * pal_sd_init() creates the SDCARD directory if it does not already exist.
 * Card size is reported as 1 GB (simulated).
 */

#include "pal_sd.h"
#include "pal_logger.h"

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <new>

#define __TAG__   "PAL_SD  "
#define SD_LOG    true

// ---------------------------------------------------------------------------
// Simulator root — resolved at runtime from SIMULATOR_SOURCE_DIR (set by
// CMake target_compile_definitions).  Falls back to cwd/SDCARD.
// ---------------------------------------------------------------------------

#ifndef SIMULATOR_SOURCE_DIR
#define SIMULATOR_SOURCE_DIR "."
#endif

static const char *k_sd_root = SIMULATOR_SOURCE_DIR "/SDCARD";

static bool s_initialized = false;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/** Build an absolute path: root + "/" + relative path (leading / optional). */
static bool _full_path(const char *rel, char *out, size_t out_len)
{
    if (!rel || !out || out_len == 0) return false;
    const char *sep = (rel[0] == '/') ? "" : "/";
    int n = snprintf(out, out_len, "%s%s%s", k_sd_root, sep, rel);
    return (n > 0 && (size_t)n < out_len);
}

/** Recursively create all directories in path (like mkdir -p). */
static int _mkdir_p(const char *path)
{
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') tmp[--len] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

/** Ensure the parent directory of `path` exists. */
static void _ensure_parent(const char *full_path)
{
    char dir[1024];
    strncpy(dir, full_path, sizeof(dir) - 1);
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        _mkdir_p(dir);
    }
}

// ---------------------------------------------------------------------------
// Directory handle (opaque)
// ---------------------------------------------------------------------------

struct SdDirHandle {
    DIR  *dp;
    char  root[1024];   // absolute path of this dir
};

// ---------------------------------------------------------------------------
// pal_sd_init / pal_sd_deinit
// ---------------------------------------------------------------------------

int32_t pal_sd_init(const pal_sd_config_t * /*config*/, pal_sd_info_t *info)
{
    if (s_initialized) return PAL_OK;

    // Create SDCARD root if missing
    struct stat st{};
    if (stat(k_sd_root, &st) != 0) {
        if (_mkdir_p(k_sd_root) != 0 && errno != EEXIST) {
            LOG_MSG_ERROR(SD_LOG, "failed to create SDCARD dir '%s': %s",
                          k_sd_root, strerror(errno));
            return -1;
        }
    }

    s_initialized = true;
    LOG_MSG_INFO(SD_LOG, "SD init OK — root: %s", k_sd_root);

    if (info) {
        info->card_size_mb    = 1024;   // simulated 1 GB card
        info->is_initialized  = true;
        strncpy(info->card_type, "SDHC (sim)", sizeof(info->card_type) - 1);
    }
    return PAL_OK;
}

int32_t pal_sd_deinit(void)
{
    s_initialized = false;
    return PAL_OK;
}

// ---------------------------------------------------------------------------
// File write / append / read
// ---------------------------------------------------------------------------

int32_t pal_sd_file_write(const char *path, const char *data, size_t size)
{
    if (!s_initialized || !path || !data) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    _ensure_parent(fp);
    FILE *f = fopen(fp, "wb");
    if (!f) {
        LOG_MSG_ERROR(SD_LOG, "write: fopen '%s' failed: %s", fp, strerror(errno));
        return -1;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    return PAL_OK;
}

int32_t pal_sd_file_append(const char *path, const char *data, size_t size)
{
    if (!s_initialized || !path || !data) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    _ensure_parent(fp);
    FILE *f = fopen(fp, "ab");
    if (!f) {
        LOG_MSG_ERROR(SD_LOG, "append: fopen '%s' failed: %s", fp, strerror(errno));
        return -1;
    }
    fwrite(data, 1, size, f);
    fclose(f);
    return PAL_OK;
}

int32_t pal_sd_file_read(const char *path, char *buffer, size_t max_size, size_t *bytes_read)
{
    if (!s_initialized || !path || !buffer) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    FILE *f = fopen(fp, "rb");
    if (!f) return -1;
    size_t n = fread(buffer, 1, max_size - 1, f);
    fclose(f);
    buffer[n] = '\0';
    if (bytes_read) *bytes_read = n;
    return PAL_OK;
}

int32_t pal_sd_file_read_line(const char *path, uint32_t line_number,
                               char *buffer, size_t max_size)
{
    if (!s_initialized || !path || !buffer) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    FILE *f = fopen(fp, "r");
    if (!f) return -1;
    uint32_t cur = 0;
    int32_t  ret = -1;
    while (fgets(buffer, (int)max_size, f)) {
        if (cur == line_number) { ret = PAL_OK; break; }
        cur++;
    }
    fclose(f);
    if (ret != PAL_OK) buffer[0] = '\0';
    return ret;
}

int32_t pal_sd_file_exists(const char *path, bool *exists)
{
    if (!exists) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    struct stat st{};
    *exists = (stat(fp, &st) == 0 && S_ISREG(st.st_mode));
    return PAL_OK;
}

int32_t pal_sd_file_delete(const char *path)
{
    if (!s_initialized || !path) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    return (unlink(fp) == 0) ? PAL_OK : -1;
}

int32_t pal_sd_file_create(const char *path)
{
    if (!s_initialized || !path) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    _ensure_parent(fp);
    FILE *f = fopen(fp, "ab");
    if (!f) return -1;
    fclose(f);
    return PAL_OK;
}

int32_t pal_sd_file_get_size(const char *path, size_t *size)
{
    if (!size) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    struct stat st{};
    if (stat(fp, &st) != 0) return -1;
    *size = (size_t)st.st_size;
    return PAL_OK;
}

// ---------------------------------------------------------------------------
// Directory operations
// ---------------------------------------------------------------------------

int32_t pal_sd_dir_open(const char *path, pal_sd_dir_handle_t *handle)
{
    if (!s_initialized || !path || !handle) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    DIR *dp = opendir(fp);
    if (!dp) {
        LOG_MSG_ERROR(SD_LOG, "dir_open '%s' failed: %s", fp, strerror(errno));
        return -1;
    }
    SdDirHandle *h = new (std::nothrow) SdDirHandle{};
    if (!h) { closedir(dp); return -1; }
    h->dp = dp;
    strncpy(h->root, fp, sizeof(h->root) - 1);
    *handle = static_cast<pal_sd_dir_handle_t>(h);
    return PAL_OK;
}

int32_t pal_sd_dir_read_next(pal_sd_dir_handle_t handle,
                              pal_sd_dir_entry_t *entry, bool *has_more)
{
    if (!handle || !entry || !has_more) return -1;
    auto *h = static_cast<SdDirHandle *>(handle);

    struct dirent *de;
    while ((de = readdir(h->dp)) != nullptr) {
        if (de->d_name[0] == '.') continue;   // skip . .. .DS_Store
        // Fill entry
        strncpy(entry->name, de->d_name, sizeof(entry->name) - 1);
        snprintf(entry->full_path, sizeof(entry->full_path),
                 "%s/%s", h->root, de->d_name);
        struct stat st{};
        stat(entry->full_path, &st);
        entry->is_directory = S_ISDIR(st.st_mode);
        entry->size         = entry->is_directory ? 0 : (size_t)st.st_size;
        *has_more = true;
        return PAL_OK;
    }
    *has_more = false;
    return PAL_OK;
}

int32_t pal_sd_dir_close(pal_sd_dir_handle_t handle)
{
    if (!handle) return PAL_OK;
    auto *h = static_cast<SdDirHandle *>(handle);
    closedir(h->dp);
    delete h;
    return PAL_OK;
}

int32_t pal_sd_dir_remove(const char *path)
{
    if (!s_initialized || !path) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    return (rmdir(fp) == 0) ? PAL_OK : -1;
}

int32_t pal_sd_dir_create(const char *path)
{
    if (!s_initialized || !path) return -1;
    char fp[1024]; if (!_full_path(path, fp, sizeof(fp))) return -1;
    _ensure_parent(fp);
    int r = mkdir(fp, 0755);
    return (r == 0 || errno == EEXIST) ? PAL_OK : -1;
}

int32_t pal_sd_get_free_mb(uint64_t *free_mb)
{
    if (!free_mb) return -1;
    *free_mb = 0;

    if (!s_initialized) return -1;

    struct statvfs sv{};
    if (statvfs(k_sd_root, &sv) != 0) {
        LOG_MSG_ERROR(SD_LOG, "statvfs failed: %s", strerror(errno));
        return -1;
    }

    // f_bavail = blocks available to unprivileged user; f_bsize = block size
    uint64_t free_bytes = (uint64_t)sv.f_bavail * (uint64_t)sv.f_bsize;
    *free_mb = free_bytes / (1024ULL * 1024ULL);
    return PAL_OK;
}
