/**
 * @file hsys_ota.h
 * @brief Platform Abstraction Layer — OTA Firmware Check Driver
 *
 * Defines the driver interface for OTA firmware check and download operations.
 * The driver is entirely isolated from any specific platform layer.
 *
 * Architecture:
 *   - This file defines hsys_ota_driver_t, a struct that holds:
 *       1. PAL implementation function pointers (filled by the platform layer,
 *          e.g. pal_esp_idf_ota_check.cpp)
 *       2. Event callback (filled by the application layer, e.g. app.cpp)
 *       3. Configuration fields (server URL, device ID, etc.)
 *
 *   - The application layer creates a hsys_ota_driver_t, assigns
 *     the PAL function pointers and its own event callback, then passes
 *     the struct to app_ota_init().
 *
 * Typical usage (in app.cpp):
 * @code
 *   // Forward declarations of ESP-IDF implementations
 *   int32_t pal_esp_idf_ota_check_version(hsys_ota_driver_t* drv,
 *                                          hsys_ota_result_t* result);
 *   int32_t pal_esp_idf_ota_download_and_flash(hsys_ota_driver_t* drv,
 *                                               const char* version);
 *
 *   static hsys_ota_driver_t _ota_driver = {
 *       .fp_check_version       = pal_esp_idf_ota_check_version,
 *       .fp_download_and_flash  = pal_esp_idf_ota_download_and_flash,
 *       .fp_on_event            = app_on_ota_event,
 *       .event_ctx              = NULL,
 *       .server_url             = "http://192.168.1.10:8080",
 *       .device_id              = DEVICE_UUID,
 *       .firmware_type          = "ferp-esp32-main",
 *       .current_version        = FW_VERSION,
 *       .timeout_ms             = 30000,
 *       .cert_pem               = NULL,
 *   };
 * @endcode
 */

#ifndef HSYS_OTA_H
#define HSYS_OTA_H

#include "pal_types.h"
#include "pal_fw_update.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants
 * ========================================================================= */

#define PAL_OTA_MAX_URL_LEN         256  /**< Max length of server_url */
#define PAL_OTA_MAX_VERSION_LEN     32   /**< Max length of a version string */
#define PAL_OTA_MAX_DEVICE_ID_LEN   64   /**< Max length of device_id UUID string */
#define PAL_OTA_MAX_FW_TYPE_LEN     64   /**< Max length of firmware_type string */
#define PAL_OTA_MAX_NOTES_LEN       128  /**< Max length of release notes */
#define PAL_OTA_CHUNK_SIZE          4096 /**< Download chunk size (bytes) */

/* =========================================================================
 * Events
 * ========================================================================= */

/**
 * @brief Events emitted by the OTA check driver to the application layer.
 * Delivered through hsys_ota_driver_t::fp_on_event.
 */
typedef enum {
    PAL_OTA_CHECK_EVENT_NONE              = 0,
    PAL_OTA_CHECK_EVENT_CHECK_STARTED,        /**< About to contact server */
    PAL_OTA_CHECK_EVENT_NO_UPDATE,            /**< Already on latest version */
    PAL_OTA_CHECK_EVENT_UPDATE_AVAILABLE,     /**< arg = hsys_ota_result_t* */
    PAL_OTA_CHECK_EVENT_DOWNLOAD_STARTED,     /**< Firmware download began */
    PAL_OTA_CHECK_EVENT_DOWNLOAD_PROGRESS,    /**< arg = hsys_ota_progress_t* */
    PAL_OTA_CHECK_EVENT_DOWNLOAD_COMPLETE,    /**< Image written, reboot required */
    PAL_OTA_CHECK_EVENT_ERROR,                /**< arg = const char* error description */
} hsys_ota_event_t;

/* =========================================================================
 * Data structures
 * ========================================================================= */

/**
 * @brief Result of a version-check call.
 * Passed as the @p arg of PAL_OTA_CHECK_EVENT_UPDATE_AVAILABLE.
 */
typedef struct {
    bool     update_available;                         /**< True when latest > current */
    char     latest_version[PAL_OTA_MAX_VERSION_LEN]; /**< Latest version on server */
    char     firmware_type[PAL_OTA_MAX_FW_TYPE_LEN];  /**< Firmware type from server */
    char     notes[PAL_OTA_MAX_NOTES_LEN];             /**< Release notes (may be empty) */
    uint32_t crc32;                                    /**< CRC32 of the firmware file */
    uint32_t file_size;                                /**< File size in bytes */
} hsys_ota_result_t;

/**
 * @brief Download progress snapshot.
 * Passed as the @p arg of PAL_OTA_CHECK_EVENT_DOWNLOAD_PROGRESS.
 */
typedef struct {
    uint32_t bytes_received; /**< Bytes received so far */
    uint32_t total_bytes;    /**< Total expected bytes (0 if unknown) */
    uint32_t crc32_running;  /**< Running CRC32 of received data */
    uint8_t  percent;        /**< 0–100 completion (0 if total unknown) */
} hsys_ota_progress_t;

/* =========================================================================
 * Driver struct (forward declaration for function pointer signatures)
 * ========================================================================= */

typedef struct hsys_ota_driver hsys_ota_driver_t;

/**
 * @brief Event callback invoked by the driver to notify the application layer.
 *
 * @param event  The event type
 * @param arg    Optional event-specific data pointer (may be NULL)
 * @param ctx    User-supplied context pointer (driver::event_ctx)
 */
typedef void (*fp_hsys_ota_on_event_t)(hsys_ota_event_t event,
                                       void*                 arg,
                                       void*                 ctx);

/**
 * @brief OTA Check Driver — the central struct wiring platform and application.
 *
 * Fields are split into three groups:
 *   - PAL implementation pointers: filled by the platform init (e.g. app.cpp assigns
 *     pal_esp_idf_* functions)
 *   - Application callback: filled by the application layer
 *   - Configuration: set by whoever constructs the struct
 */
struct hsys_ota_driver {
    /* ---- PAL implementation function pointers (set by application via platform layer) ---- */

    /**
     * @brief Check if a firmware update is available on the server.
     * Performs a POST to /api/v1/firmware/check and populates @p result.
     * @return PAL_OK on success, negative error code on failure.
     */
    int32_t (*fp_check_version)(hsys_ota_driver_t* drv,
                                hsys_ota_result_t* result);

    /**
     * @brief Download the specified firmware version and write it to flash.
     * Streams from /api/v1/firmware/download, verifies CRC32, commits OTA slot.
     * Does NOT restart the device.
     * @return PAL_OK on success, negative error code on failure.
     */
    int32_t (*fp_download_and_flash)(hsys_ota_driver_t* drv,
                                     const char*             version);

    /* ---- Application layer event callback ---- */

    fp_hsys_ota_on_event_t fp_on_event; /**< Callback into the app layer for all events */
    void*                 event_ctx;   /**< Passed verbatim to fp_on_event as ctx */

    /* ---- Flash writer driver (set by application layer) ---- */

    /**
     * @brief Pointer to the flash-write driver.
     *
     * Wraps the pal_fw_update_* session lifecycle so that hsys_ota never
     * calls PAL symbols directly. Assign a statically-initialised
     * hsys_fw_update_driver_t from app.cpp.
     * Must not be NULL when fp_download_and_flash is called.
     */
    hsys_fw_update_driver_t* fw_drv;

    /* ---- Configuration getters (implemented by the application layer) ---- */

    /**
     * @brief Return the OTA server base URL  e.g. "http://192.168.1.10:8080"
     * Called by the PAL implementation before every HTTP request.
     */
    const char* (*fp_get_server_url)();

    /**
     * @brief Return the device UUID string  e.g. "00000000-0000-0000-0000-000000000000"
     */
    const char* (*fp_get_device_id)();

    /**
     * @brief Return the firmware type string  e.g. "ferp-esp32-main"
     */
    const char* (*fp_get_firmware_type)();

    /**
     * @brief Return the currently running firmware version string  e.g. "2.3.51"
     */
    const char* (*fp_get_current_version)();

    /**
     * @brief Return the HTTP request timeout in milliseconds (0 → use default 30 000 ms)
     */
    uint32_t    (*fp_get_timeout_ms)();

    /**
     * @brief Return the PEM CA certificate string for HTTPS, or NULL for plain HTTP
     */
    const char* (*fp_get_cert_pem)();
};

/* =========================================================================
 * Driver helper — emit an event through the driver's callback
 * ========================================================================= */

/**
 * @brief Convenience inline to fire an event through a driver instance.
 * Safe to call even if fp_on_event is NULL.
 */
static inline void hsys_ota_emit(hsys_ota_driver_t* drv,
                                       hsys_ota_event_t   event,
                                       void*                   arg)
{
    if (drv && drv->fp_on_event) {
        drv->fp_on_event(event, arg, drv->event_ctx);
    }
}

/* =========================================================================
 * ESP-IDF platform implementation — assign these into the driver struct
 * ========================================================================= */

int32_t pal_esp_idf_ota_check_version(hsys_ota_driver_t* drv,
                                       hsys_ota_result_t* result);

int32_t pal_esp_idf_ota_download_and_flash(hsys_ota_driver_t* drv,
                                            const char*             version);

#ifdef __cplusplus
}
#endif

#endif /*  */
