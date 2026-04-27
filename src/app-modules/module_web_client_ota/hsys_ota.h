/**
 * @file hsys_ota.h
 * @brief Cloud-polling OTA check and download engine.
 *
 * Provides two blocking functions used by ModuleWebClientOta:
 *
 *   hsys_ota_check_version()   — POST /api/v1/firmware/check
 *   hsys_ota_download_to_fs()  — GET  /api/v1/firmware/download (streaming)
 *
 * Both functions use only PAL interfaces (pal_http_client) and write the
 * received binary via the ota_fs_driver_t handed to the module by OtaModule
 * through MsgOtaDriverResponse.  No flash write APIs are referenced directly.
 *
 * Callers fill an hsys_ota_cfg_t before each call.  All strings are
 * null-terminated; cert_pem is a pointer to a static PEM string (may be NULL
 * for plain HTTP).
 *
 * CRC32: poly 0xEDB88320, initial 0, no final XOR — must match server calc.
 */

#pragma once

#include "FileSystemDriver.h"   /* ota_fs_driver_t, OTA_FS_OK */
#include "pal_types.h"          /* PAL_OK, PAL_ERROR */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * String length limits
 * ========================================================================= */

#define HSYS_OTA_MAX_URL_LEN          256
#define HSYS_OTA_MAX_VERSION_LEN       32
#define HSYS_OTA_MAX_DEVICE_ID_LEN     64
#define HSYS_OTA_MAX_FW_TYPE_LEN       64
#define HSYS_OTA_MAX_NOTES_LEN        128

/* =========================================================================
 * Configuration passed to each call (value-based — no function pointers)
 * ========================================================================= */

/**
 * @brief All parameters needed to contact the OTA server.
 *
 * Fill this struct before each call.  The module builds it from app_config
 * and the active web_ota_target_cfg_t so that per-call values are always
 * fresh.
 */
typedef struct {
    char        server_url     [HSYS_OTA_MAX_URL_LEN];      ///< Base URL, e.g. "https://ota.example.com"
    char        device_id      [HSYS_OTA_MAX_DEVICE_ID_LEN]; ///< Device UUID string
    char        firmware_type  [HSYS_OTA_MAX_FW_TYPE_LEN];  ///< e.g. "ferp-esp32-main"
    char        current_version[HSYS_OTA_MAX_VERSION_LEN];  ///< Currently running version
    uint32_t    timeout_ms;                                  ///< HTTP timeout (0 → 30 000 ms default)
    const char *cert_pem;                                    ///< PEM root-CA (static lifetime, NULL = plain HTTP)
} hsys_ota_cfg_t;

/* =========================================================================
 * Check result
 * ========================================================================= */

/**
 * @brief Populated by hsys_ota_check_version() on success.
 */
typedef struct {
    bool     update_available;                               ///< True when server version > current
    char     latest_version[HSYS_OTA_MAX_VERSION_LEN];      ///< Server's latest version string
    char     notes         [HSYS_OTA_MAX_NOTES_LEN];        ///< Release notes (may be empty)
    uint32_t crc32;                                          ///< Expected CRC32 of the firmware file
    uint32_t file_size;                                      ///< Expected file size in bytes (0 if unknown)
} hsys_ota_check_result_t;

/* =========================================================================
 * Progress callback (used by hsys_ota_download_to_fs)
 * ========================================================================= */

/**
 * @brief Called for each received chunk during hsys_ota_download_to_fs().
 *
 * @param bytes_written  Cumulative bytes written to the fs driver so far.
 * @param total_bytes    Expected total (0 if Content-Length was absent).
 * @param percent        0–100 completion, or 0 if total unknown.
 * @param arg            Caller-supplied context pointer.
 */
typedef void (*hsys_ota_progress_cb_t)(uint32_t bytes_written,
                                       uint32_t total_bytes,
                                       uint8_t  percent,
                                       void    *arg);

/* =========================================================================
 * API
 * ========================================================================= */

/**
 * @brief Check the OTA server for an available firmware update.
 *
 * Performs a POST to <server_url>/api/v1/firmware/check and parses the JSON
 * response.  Blocks until the response arrives or the timeout fires.
 *
 * @param[in]  cfg  Filled configuration struct.
 * @param[out] out  Populated on PAL_OK return.
 * @return PAL_OK on success (check was performed; out.update_available reports
 *         whether an update exists), or a negative PAL error code on HTTP /
 *         parse failure.
 */
int32_t hsys_ota_check_version(const hsys_ota_cfg_t      *cfg,
                                hsys_ota_check_result_t   *out);

/**
 * @brief Download a firmware image and stream it into an ota_fs_driver_t.
 *
 * Performs a GET to:
 *   <server_url>/api/v1/firmware/download?firmware_type=...&version=...&device_id=...
 *
 * The binary stream is delivered to the fs driver:
 *   fopen  → fwrite × N → fclose      on success
 *   fopen  → fwrite × N → ferase      on transport/CRC error
 *
 * Calls @p cb for every received chunk (may be NULL).
 * Blocks until the download completes or the timeout fires.
 *
 * @param[in] cfg      Filled configuration struct.
 * @param[in] version  Version string returned by hsys_ota_check_version().
 * @param[in] fs_drv   Driver handed by OtaModule via MsgOtaDriverResponse.
 * @param[in] fs_ctx   Context pointer from MsgOtaDriverResponse.
 * @param[in] cb       Progress callback (may be NULL).
 * @param[in] cb_arg   Passed verbatim to cb.
 * @return PAL_OK on success (image fully written and committed),
 *         or a negative PAL error code on failure.
 */
int32_t hsys_ota_download_to_fs(const hsys_ota_cfg_t      *cfg,
                                 const char                *version,
                                 const ota_fs_driver_t     *fs_drv,
                                 void                      *fs_ctx,
                                 hsys_ota_progress_cb_t     cb,
                                 void                      *cb_arg);

#ifdef __cplusplus
}
#endif
