// pal_mac_nvs.cpp
//
// macOS/Linux simulator implementation of pal_nvs.h.
//
// Key-value pairs are stored as individual binary files:
//   On-device:  NVS flash (namespace/key)
//   Simulator:  <cwd>/SPIFFS/nvs/<namespace>/<key>
//
// int64 values are stored as 8 raw bytes (little-endian host order).
// Blobs are stored as raw bytes.

#include "pal_nvs.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>

// ── Internal state ─────────────────────────────────────────────────────────────

static bool s_initialized = false;
static char s_root[512];   // absolute path to <cwd>/SPIFFS/nvs

// ── Helpers ───────────────────────────────────────────────────────────────────

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

// Resolve (namespace, key) to an absolute host file path.
static int _resolve(const char *ns, const char *key, char *out, size_t out_size)
{
    if (!s_initialized || !ns || !key || !out) return -1;
    int n = snprintf(out, out_size, "%s/%s/%s", s_root, ns, key);
    return (n > 0 && (size_t)n < out_size) ? 0 : -1;
}

// Ensure the namespace directory exists.
static int _ensure_ns(const char *ns)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s", s_root, ns);
    return _mkdirs(dir);
}

// ── Public API ────────────────────────────────────────────────────────────────

int32_t pal_nvs_init(void)
{
    if (s_initialized) return PAL_OK;

    char cwd[256] = ".";
    getcwd(cwd, sizeof(cwd));
    snprintf(s_root, sizeof(s_root), "%s/SPIFFS/nvs", cwd);

    if (_mkdirs(s_root) != 0) return PAL_ERROR_IO;

    s_initialized = true;
    return PAL_OK;
}

int32_t pal_nvs_write_i64(const char *ns, const char *key, int64_t value)
{
    if (!ns || !key) return PAL_ERROR_INVALID;
    if (!s_initialized) return PAL_ERROR_INIT;

    if (_ensure_ns(ns) != 0) return PAL_ERROR_IO;

    char path[512];
    if (_resolve(ns, key, path, sizeof(path)) != 0) return PAL_ERROR_IO;

    FILE *f = fopen(path, "wb");
    if (!f) return PAL_ERROR_IO;
    size_t w = fwrite(&value, 1, sizeof(value), f);
    fclose(f);
    return (w == sizeof(value)) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_nvs_read_i64(const char *ns, const char *key, int64_t *value)
{
    if (!ns || !key || !value) return PAL_ERROR_INVALID;
    if (!s_initialized) return PAL_ERROR_INIT;

    char path[512];
    if (_resolve(ns, key, path, sizeof(path)) != 0) return PAL_ERROR_IO;

    FILE *f = fopen(path, "rb");
    if (!f) return PAL_ERROR_NOT_FOUND;

    size_t r = fread(value, 1, sizeof(*value), f);
    fclose(f);
    return (r == sizeof(*value)) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_nvs_write_blob(const char *ns, const char *key,
                            const void *data, size_t size)
{
    if (!ns || !key || !data || size == 0) return PAL_ERROR_INVALID;
    if (!s_initialized) return PAL_ERROR_INIT;

    if (_ensure_ns(ns) != 0) return PAL_ERROR_IO;

    char path[512];
    if (_resolve(ns, key, path, sizeof(path)) != 0) return PAL_ERROR_IO;

    FILE *f = fopen(path, "wb");
    if (!f) return PAL_ERROR_IO;
    size_t w = fwrite(data, 1, size, f);
    fclose(f);
    return (w == size) ? PAL_OK : PAL_ERROR_IO;
}

int32_t pal_nvs_read_blob(const char *ns, const char *key,
                           void *buf, size_t max_size, size_t *bytes_read)
{
    if (!ns || !key || !buf || max_size == 0) return PAL_ERROR_INVALID;
    if (!s_initialized) return PAL_ERROR_INIT;

    char path[512];
    if (_resolve(ns, key, path, sizeof(path)) != 0) return PAL_ERROR_IO;

    FILE *f = fopen(path, "rb");
    if (!f) return PAL_ERROR_NOT_FOUND;

    // Check actual size first
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz < 0) { fclose(f); return PAL_ERROR_IO; }
    if ((size_t)sz > max_size) { fclose(f); return PAL_ERROR_OVERFLOW; }

    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (r != (size_t)sz) return PAL_ERROR_IO;
    if (bytes_read) *bytes_read = r;
    return PAL_OK;
}

int32_t pal_nvs_erase_key(const char *ns, const char *key)
{
    if (!ns || !key) return PAL_ERROR_INVALID;
    if (!s_initialized) return PAL_ERROR_INIT;

    char path[512];
    if (_resolve(ns, key, path, sizeof(path)) != 0) return PAL_ERROR_IO;

    if (remove(path) != 0 && errno != ENOENT) return PAL_ERROR_IO;
    return PAL_OK;
}
