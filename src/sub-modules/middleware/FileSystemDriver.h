// FileSystemDriver.h
//
// OTA binary-streaming filesystem abstraction.
//
// ota_fs_driver_t is the interface through which OtaModule hands a concrete
// write backend (ESP-IDF OTA partition, SPIFFS staging file, …) to the OTA
// source without the source knowing any platform details.
//
// Usage pattern:
//   1. OtaModule hands driver + ctx to the source via MsgOtaDriverResponse.
//   2. Source calls: fopen → fwrite × N → fclose  (or ferase on error).
//   3. Source never touches the ctx struct directly.
//
// Error codes purposely kept separate from pal_err_t to avoid a hard
// dependency on the PAL layer from this middleware header.

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

typedef enum {
    OTA_FS_OK              =   0,
    OTA_FS_ERR_NOT_OPEN    =  -1,
    OTA_FS_ERR_WRITE_FAIL  =  -2,
    OTA_FS_ERR_READ_FAIL   =  -3,
    OTA_FS_ERR_ERASE_FAIL  =  -4,
    OTA_FS_ERR_INVALID_ARG =  -5,
    OTA_FS_ERR_NO_SPACE    =  -6,
    OTA_FS_ERR_TIMEOUT     =  -7,
    OTA_FS_ERR_UNKNOWN     = -99,
} ota_fs_err_t;

// ---------------------------------------------------------------------------
// Open mode
// ---------------------------------------------------------------------------

typedef enum {
    OTA_FS_OPEN_WRITE  = 0,   ///< Overwrite / create new
    OTA_FS_OPEN_APPEND = 1,   ///< Append to existing
    OTA_FS_OPEN_READ   = 2,   ///< Read-only
} ota_fs_open_mode_t;

// ---------------------------------------------------------------------------
// Driver function-pointer table
//
//   fopen  — prepare the write target (select OTA slot, open file, etc.)
//   fwrite — write one sequential chunk of binary data
//   fappend— append one chunk (may be same as fwrite for some targets)
//   fclose — finalise and commit (validate image, flush, close handle)
//   ferase — abort and clean up a partial write (called on timeout / error)
//   fread  — optional read-back; return OTA_FS_ERR_INVALID_ARG if unused
//
// ctx is the opaque void* from ota_target_desc_t; passed unchanged to every call.
// ---------------------------------------------------------------------------

typedef struct {
    ota_fs_err_t (*fopen) (void *ctx, const char *path, ota_fs_open_mode_t mode);
    ota_fs_err_t (*fclose)(void *ctx);
    ota_fs_err_t (*fwrite) (void *ctx, const uint8_t *data, uint32_t len);
    ota_fs_err_t (*fappend)(void *ctx, const uint8_t *data, uint32_t len);
    ota_fs_err_t (*fread)  (void *ctx, uint8_t *buf, uint32_t len, uint32_t *out_len);
    ota_fs_err_t (*ferase) (void *ctx);
} ota_fs_driver_t;

#ifdef __cplusplus
}
#endif
