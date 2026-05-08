/**
 * pal_mac_spiffs.cpp
 *
 * macOS/Linux simulator implementation of pal_spiffs.h.
 *
 * The "SPIFFS partition" is emulated as a subdirectory of the working
 * directory.  The layout mirrors the embedded layout:
 *
 *   On-device:  /spiffs/Configs/DeviceConfigs.json
 *   Simulator:  <cwd>/SPIFFS/spiffs/Configs/DeviceConfigs.json
 *
 * Paths passed to these functions must be relative (no leading '/').
 * For example:  pal_spiffs_file_write("Configs/DeviceConfigs.json", ...)
 */

#include "pal_spiffs.h"
#include "pal_types.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

// ── Internal state ────────────────────────────────────────────────────────────

static bool   s_initialized = false;
static char   s_root[512];   // absolute path to emulated SPIFFS root dir

// ── Helpers ───────────────────────────────────────────────────────────────────

/** Ensure every directory component in `path` exists (like mkdir -p). */
static int _mkdirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/**
 * Resolve a SPIFFS-relative path to an absolute host path.
 *
 * Input:   "Configs/DeviceConfigs.json"  (no leading slash)
 * Output:  "<cwd>/SPIFFS/spiffs/Configs/DeviceConfigs.json"
 */
static int _resolve(const char *spiffs_path, char *out, size_t out_size)
{
    if (!s_initialized || !spiffs_path || !out) return -1;

    // Strip a leading '/' if the caller passed "/spiffs/..." by mistake
    if (spiffs_path[0] == '/') ++spiffs_path;

    int n = snprintf(out, out_size, "%s/%s", s_root, spiffs_path);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

/** Ensure the directory portion of `full_path` exists. */
static int _ensure_parent(const char *full_path)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s", full_path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        return _mkdirs(dir);
    }
    return 0;
}

// ── Public API ────────────────────────────────────────────────────────────────

int32_t pal_spiffs_init(const pal_spiffs_config_t* config, pal_spiffs_info_t* info)
{
    if (!config) return PAL_ERROR_INVALID;

    // Build the root path: <cwd>/SPIFFS/<base_path>
    // base_path is typically "/spiffs"; strip the leading '/'
    const char *bp = config->base_path ? config->base_path : "/spiffs";
    if (bp[0] == '/') ++bp;

    char cwd[256] = ".";
    getcwd(cwd, sizeof(cwd));
    snprintf(s_root, sizeof(s_root), "%s/SPIFFS/%s", cwd, bp);

    if (_mkdirs(s_root) != 0) {
        return PAL_ERROR_IO;
    }

    s_initialized = true;

    if (info) {
        memset(info, 0, sizeof(*info));
        info->total_bytes    = 1024 * 1024;   // fake 1 MiB
        info->used_bytes     = 0;
        info->free_bytes     = info->total_bytes;
        info->is_initialized = true;
    }

    return PAL_OK;
}

int32_t pal_spiffs_deinit(void)
{
    s_initialized = false;
    s_root[0] = '\0';
    return PAL_OK;
}

int32_t pal_spiffs_file_write(const char* path, const uint8_t* data, size_t size)
{
    if (!s_initialized || !path || !data) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(path, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;
    if (_ensure_parent(full) != 0) return PAL_ERROR_IO;

    FILE *f = fopen(full, "wb");
    if (!f) return PAL_ERROR_IO;

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    return (written == size) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_spiffs_file_append(const char* path, const uint8_t* data, size_t size)
{
    if (!s_initialized || !path || !data) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(path, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;
    if (_ensure_parent(full) != 0) return PAL_ERROR_IO;

    FILE *f = fopen(full, "ab");
    if (!f) return PAL_ERROR_IO;

    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    return (written == size) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_spiffs_file_read(const char* path, uint8_t* buffer, size_t max_size, size_t* bytes_read)
{
    if (!s_initialized || !path || !buffer) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(path, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;

    FILE *f = fopen(full, "rb");
    if (!f) return PAL_ERROR_NOT_FOUND;

    size_t n = fread(buffer, 1, max_size, f);
    fclose(f);

    if (bytes_read) *bytes_read = n;
    return PAL_OK;
}

int32_t pal_spiffs_file_read_at(const char* path, size_t offset, uint8_t *buffer, size_t max_size, size_t *bytes_read)
{
    if (!s_initialized || !path || !buffer) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(path, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;

    FILE *f = fopen(full, "rb");
    if (!f) return PAL_ERROR_NOT_FOUND;

    if (offset > 0) fseek(f, (long)offset, SEEK_SET);
    size_t n = fread(buffer, 1, max_size, f);
    fclose(f);

    if (bytes_read) *bytes_read = n;
    return PAL_OK;
}

int32_t pal_spiffs_file_exists(const char* path, bool* exists)
{
    if (!s_initialized || !path || !exists) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(path, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;

    struct stat st;
    *exists = (stat(full, &st) == 0 && S_ISREG(st.st_mode));
    return PAL_OK;
}

int32_t pal_spiffs_file_delete(const char* path)
{
    if (!s_initialized || !path) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(path, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;

    if (remove(full) != 0) return PAL_ERROR_NOT_FOUND;
    return PAL_OK;
}

int32_t pal_spiffs_file_create(const char* path)
{
    if (!s_initialized || !path) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(path, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;
    if (_ensure_parent(full) != 0) return PAL_ERROR_IO;

    FILE *f = fopen(full, "ab");   // "ab" creates without truncating
    if (!f) return PAL_ERROR_IO;
    fclose(f);
    return PAL_OK;
}

int32_t pal_spiffs_file_get_size(const char* path, size_t* size)
{
    if (!s_initialized || !path || !size) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(path, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;

    struct stat st;
    if (stat(full, &st) != 0) return PAL_ERROR_NOT_FOUND;
    *size = (size_t)st.st_size;
    return PAL_OK;
}

// ── Raw handle API ────────────────────────────────────────────────────────────

int32_t pal_spiffs_raw_open(const char* filepath, void** handler, uint32_t timeout_ms, const char* mode)
{
    (void)timeout_ms;
    if (!s_initialized || !filepath || !handler || !mode) return PAL_ERROR_INVALID;

    char full[512];
    if (_resolve(filepath, full, sizeof(full)) != 0) return PAL_ERROR_INVALID;
    if (_ensure_parent(full) != 0) return PAL_ERROR_IO;

    FILE *f = fopen(full, mode);
    if (!f) return PAL_ERROR_IO;

    *handler = (void*)f;
    return PAL_OK;
}

int32_t pal_spiffs_raw_read(void* handler, char content[], size_t* content_size, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!handler || !content || !content_size) return PAL_ERROR_INVALID;

    size_t n = fread(content, 1, *content_size, (FILE*)handler);
    *content_size = n;
    return PAL_OK;
}

int32_t pal_spiffs_raw_write(void* handler, const char content[], size_t* content_size, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!handler || !content || !content_size) return PAL_ERROR_INVALID;

    size_t n = fwrite(content, 1, *content_size, (FILE*)handler);
    *content_size = n;
    return (n > 0) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_spiffs_raw_close(void* handler, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!handler) return PAL_ERROR_INVALID;
    fclose((FILE*)handler);
    return PAL_OK;
}

// ── Filesystem info ───────────────────────────────────────────────────────────

int32_t pal_spiffs_get_info(pal_spiffs_info_t* info)
{
    if (!info) return PAL_ERROR_INVALID;
    info->total_bytes    = 1024 * 1024;
    info->used_bytes     = 0;
    info->free_bytes     = info->total_bytes;
    info->is_initialized = s_initialized;
    return PAL_OK;
}

int32_t pal_spiffs_format(void)
{
    // In the simulator we just report success — no actual formatting needed
    return PAL_OK;
}
