/**
 * @file pal_fw_update.h
 * @brief Platform Abstraction Layer - Firmware Update Interface
 *
 * Platform-independent interface for over-the-air (OTA) firmware updates
 * targeting the device this code is running on (phase 2 delivery).
 *
 * Phase 2 assumes the binary data is already available and verified by the
 * caller (e.g. received over HTTP). This layer is responsible only for
 * writing it to the appropriate flash slot, validating it, and committing
 * it as the next boot target.
 *
 * Typical usage:
 * @code
 *   pal_fw_update_handle_t h;
 *   pal_fw_update_begin(&h);
 *   // ... for each received chunk:
 *   pal_fw_update_write(h, data, len);
 *   // ... after last chunk:
 *   pal_fw_update_end(h);
 *   // caller decides when to restart via pal_system_restart() or equivalent
 * @endcode
 */

#ifndef PAL_FW_UPDATE_H
#define PAL_FW_UPDATE_H

#include "pal_types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                            TYPE DEFINITIONS                               */
/*===========================================================================*/

/* -------------------------------------------------------------------------
 * Binary / partition metadata
 * -------------------------------------------------------------------------
 * pal_bin_info_t holds the descriptive fields that every ESP-IDF firmware
 * binary embeds at a fixed offset (esp_app_desc_t, offset 0x20).
 * It is intentionally platform-agnostic: field names mirror the Python
 * bin_info.py EspAppDesc dataclass so the two stay in sync.
 * ------------------------------------------------------------------------- */

/**
 * @brief Which firmware slot this info was read from.
 */
typedef enum {
    PAL_BIN_SLOT_RUNNING  = 0,  ///< The partition the device is currently running
    PAL_BIN_SLOT_NEXT     = 1,  ///< The next OTA update partition (may be empty)
} pal_bin_slot_t;

/**
 * @brief Firmware binary metadata read directly from a flash partition.
 *
 * All string fields are null-terminated. Lengths match the ESP-IDF
 * esp_app_desc_t structure exactly.
 */
typedef struct {
    char     version[32];       ///< PROJECT_VER / FW_VERSION embedded at build time
    char     project_name[32];  ///< CMake project() name
    char     build_date[16];    ///< "MMM DD YYYY"
    char     build_time[16];    ///< "HH:MM:SS"
    char     idf_version[32];   ///< ESP-IDF version string  e.g. "v5.5.3"
    uint8_t  elf_sha256[32];    ///< SHA-256 of the ELF file (raw bytes)
    uint32_t secure_version;    ///< Anti-rollback counter
    bool     valid;             ///< true if the slot contained a readable descriptor
} pal_bin_info_t;

/**
 * @brief Opaque handle for an active firmware update session.
 * Obtained from pal_fw_update_begin(). Must be passed to all subsequent calls.
 */
typedef void* pal_fw_update_handle_t;

/**
 * @brief State of the firmware update session.
 */
typedef enum {
    PAL_FW_UPDATE_STATE_IDLE        = 0, ///< No update has been started
    PAL_FW_UPDATE_STATE_IN_PROGRESS,     ///< Update session open, data being written
    PAL_FW_UPDATE_STATE_SUCCESS,         ///< end() succeeded, boot slot committed
    PAL_FW_UPDATE_STATE_FAILED,          ///< An error occurred during the session
    PAL_FW_UPDATE_STATE_ABORTED,         ///< Session was explicitly aborted
} pal_fw_update_state_t;

/**
 * @brief Snapshot of the current (or most recent) update session.
 * Safe to query at any time, even between sessions.
 */
typedef struct {
    pal_fw_update_state_t state;         ///< Current state
    uint32_t              bytes_written; ///< Running total of bytes written so far
    int32_t               error_code;   ///< PAL_OK (0) = no error; negative = error
    char                  status_str[64];///< Human-readable status, suitable for HTTP response body
} pal_fw_update_status_t;

/*===========================================================================*/
/*                          SESSION LIFECYCLE                                */
/*===========================================================================*/

/**
 * @brief Begin a firmware update session.
 *
 * Selects the next available OTA flash slot and prepares it for writing.
 * Only one session may be active at a time; calling begin() while a session
 * is already in progress returns PAL_ERROR_BUSY.
 *
 * @param[out] handle  Receives the opaque session handle on success.
 *                     Set to NULL on failure.
 * @return PAL_OK on success, PAL_ERROR_BUSY if already in progress,
 *         PAL_ERROR_NOT_FOUND if no OTA partition exists,
 *         PAL_ERROR_INIT on platform-level failure.
 */
int32_t pal_fw_update_begin(pal_fw_update_handle_t* handle);

/**
 * @brief Write the next sequential chunk of firmware binary data.
 *
 * Must be called in order from offset 0. Chunks do not need to be
 * equal in size but must be contiguous with no gaps or overlaps.
 *
 * @param handle  Session handle obtained from pal_fw_update_begin().
 * @param data    Pointer to chunk data buffer.
 * @param len     Number of valid bytes in @p data.
 * @return PAL_OK on success, PAL_ERROR_INVALID if handle is NULL or session
 *         is not in progress, PAL_ERROR_IO on write failure.
 */
int32_t pal_fw_update_write(pal_fw_update_handle_t handle,
                            const uint8_t*         data,
                            size_t                 len);

/**
 * @brief Finalise the update session.
 *
 * Validates the written image (signature / hash check where supported by
 * the platform) and marks the OTA slot as the next boot target.
 * The session handle becomes invalid after this call regardless of outcome.
 * Does NOT restart the device — the caller is responsible for triggering
 * a restart through the appropriate system-level API.
 *
 * @param handle  Session handle obtained from pal_fw_update_begin().
 * @return PAL_OK if the image is valid and the boot slot was set successfully.
 *         PAL_ERROR if image validation failed.
 *         PAL_ERROR_INIT if setting the boot partition failed.
 */
int32_t pal_fw_update_end(pal_fw_update_handle_t handle);

/**
 * @brief Abort an in-progress update session and release all resources.
 *
 * Safe to call even if begin() only partially succeeded.
 * The session handle becomes invalid after this call.
 *
 * @param handle  Session handle obtained from pal_fw_update_begin().
 * @return PAL_OK on success, PAL_ERROR_INVALID if handle is NULL.
 */
int32_t pal_fw_update_abort(pal_fw_update_handle_t handle);

/*===========================================================================*/
/*                              STATUS QUERY                                 */
/*===========================================================================*/

/**
 * @brief Get the status of the current or most recently completed session.
 *
 * Safe to call at any time, including when no session is active (returns
 * the outcome of the last session, or IDLE if no session has ever run).
 *
 * @param[out] status  Filled with the current state, bytes written, error code
 *                     and a human-readable status string.
 * @return PAL_OK always (output is always valid).
 */
int32_t pal_fw_update_get_status(pal_fw_update_status_t* status);

/*===========================================================================*/
/*                          BOOT CONFIRMATION                                */
/*===========================================================================*/

/**
 * @brief Confirm the currently running app image as valid, cancelling any
 *        pending OTA rollback.
 *
 * ESP-IDF's bootloader-level app rollback marks a freshly OTA'd image as
 * "pending verify" on first boot. If that image resets/crashes again before
 * this function is called, the bootloader treats it as bad and permanently
 * reverts to the previous OTA slot on every subsequent boot — regardless of
 * whether the actual cause of the reset was unrelated to the new firmware.
 *
 * Call this once, after boot reaches a point you're confident represents a
 * genuinely working device (e.g. MQTT connected) — not merely "did not crash
 * yet". Safe to call unconditionally/repeatedly: a no-op once already
 * confirmed, and a no-op on platforms without rollback support (e.g. the
 * simulator, or a bootloader built with rollback disabled).
 *
 * @return PAL_OK if confirmed (or already confirmed / not applicable).
 *         PAL_ERROR on platform-reported failure.
 */
int32_t pal_fw_update_mark_valid(void);

/*===========================================================================*/
/*                          FLASH WRITER DRIVER                              */
/*===========================================================================*/

/**
 * @brief Driver struct for firmware flash-write operations.
 *
 * Mirrors the pal_fw_update_* session lifecycle as function pointers so that
 * any caller (e.g. hsys_ota) is fully decoupled from the PAL symbol names.
 *
 * Typical wiring in app.cpp:
 * @code
 *   static int32_t _fw_begin (pal_fw_update_handle_t* h)              { return pal_fw_update_begin(h);      }
 *   static int32_t _fw_write (pal_fw_update_handle_t  h,
 *                              const uint8_t* d, size_t n)             { return pal_fw_update_write(h, d, n); }
 *   static int32_t _fw_end   (pal_fw_update_handle_t  h)              { return pal_fw_update_end(h);        }
 *   static int32_t _fw_abort (pal_fw_update_handle_t  h)              { return pal_fw_update_abort(h);      }
 *
 *   static hsys_fw_update_driver_t _fw_drv = {
 *       .fp_begin = _fw_begin,
 *       .fp_write = _fw_write,
 *       .fp_end   = _fw_end,
 *       .fp_abort = _fw_abort,
 *   };
 * @endcode
 */
typedef struct {
    /**
     * @brief Open a new flash-write session.
     * Equivalent to pal_fw_update_begin().
     */
    int32_t (*fp_begin)(pal_fw_update_handle_t* handle);

    /**
     * @brief Write the next sequential chunk.
     * Equivalent to pal_fw_update_write().
     */
    int32_t (*fp_write)(pal_fw_update_handle_t handle,
                        const uint8_t*         data,
                        size_t                 len);

    /**
     * @brief Validate and commit the written image.
     * Equivalent to pal_fw_update_end().
     */
    int32_t (*fp_end)(pal_fw_update_handle_t handle);

    /**
     * @brief Abort and discard the in-progress session.
     * Equivalent to pal_fw_update_abort().
     */
    int32_t (*fp_abort)(pal_fw_update_handle_t handle);
} hsys_fw_update_driver_t;

/*===========================================================================*/
/*                         BINARY INFO QUERIES                               */
/*===========================================================================*/

/**
 * @brief Read firmware binary metadata from a flash partition.
 *
 * Reads the esp_app_desc_t descriptor embedded in the specified OTA slot
 * and fills @p info without loading or executing any code.
 * Safe to call at any time, including during an active OTA session.
 *
 * @param[in]  slot  Which partition to read: PAL_BIN_SLOT_RUNNING or
 *                   PAL_BIN_SLOT_NEXT.
 * @param[out] info  Filled with the metadata on success. On failure all
 *                   string fields are empty and info->valid is false.
 * @return PAL_OK on success.
 *         PAL_ERROR_NOT_FOUND if the requested slot does not exist or
 *         contains no valid descriptor.
 *         PAL_ERROR_INVALID if @p info is NULL.
 */
int32_t pal_fw_update_get_bin_info(pal_bin_slot_t slot, pal_bin_info_t* info);

/**
 * @brief Print firmware binary metadata to the log.
 *
 * Reads the descriptor from the specified slot and emits one log line per
 * field at INFO level. Intended as a single startup diagnostic call.
 *
 * @param slot  Which partition to read (PAL_BIN_SLOT_RUNNING or _NEXT).
 */
void pal_fw_update_print_bin_info(pal_bin_slot_t slot);

#ifdef __cplusplus
}
#endif

#endif /* PAL_FW_UPDATE_H */
