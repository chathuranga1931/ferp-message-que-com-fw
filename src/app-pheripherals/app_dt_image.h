// app_dt_image.h
//
// Storage for the DispTap (DT) board firmware images — bootloader.bin,
// partition_table.bin, distap_esp32.bin — that the main board flashes onto
// the DT ESP32 over UART.
//
// The images live on the SD card under /dtfw/ only.  (They used to live in
// SPIFFS under esp32/, where the ~290 KB application image filled the 512 KB
// partition until SPIFFS could no longer rewrite even the config file; old
// copies there are removed with MsgSpiffsCleanDt.)  Without a mounted SD card
// DT firmware OTA is refused and the flasher finds no image.
//
//   OTA write  : app_dt_image_write_begin/_write/_commit/_abort
//                → /dtfw/<name>.tmp, verified (size + CRC32 read-back), then
//                  renamed over /dtfw/<name>.  The previous image stays usable
//                  until the new one is complete and verified.
//   Flash read : app_dt_image_open/_read_at/_close.  open() holds the image
//                lock until close(), so a commit never swaps a file mid-flash.
//
// All SD access goes through app_sd; its mutex is held per chunk only, so SD
// logging carries on while an image is written or flashed.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define APP_DT_IMAGE_OK             (0)
#define APP_DT_IMAGE_ERR_NOT_FOUND  (-1)
#define APP_DT_IMAGE_ERR_IO         (-2)
#define APP_DT_IMAGE_ERR_BUSY       (-3)
#define APP_DT_IMAGE_ERR_INVALID    (-4)
#define APP_DT_IMAGE_ERR_VERIFY     (-5)
#define APP_DT_IMAGE_ERR_NO_SD      (-6)

#define APP_DT_IMAGE_SD_DIR         "/dtfw"   ///< SD card directory

#ifdef __cplusplus
extern "C" {
#endif

/** Create the image lock.  Call once from app_init(), before any task runs. */
void app_dt_image_init(void);

// ── OTA write (one session at a time) ────────────────────────────────────────

/**
 * Start writing image @p name (file name only, e.g. "distap_esp32.bin";
 * a leading directory such as "esp32/" is ignored).
 * Returns APP_DT_IMAGE_ERR_NO_SD when no SD card is mounted.
 */
int32_t app_dt_image_write_begin(const char *name);
int32_t app_dt_image_write(const uint8_t *data, size_t len);
/** Verify the written image and make it the current one. */
int32_t app_dt_image_write_commit(void);
/** Discard a partial image. */
void    app_dt_image_write_abort(void);

// ── Flash read ───────────────────────────────────────────────────────────────

/** Lock and open image @p name; *size receives its length. */
int32_t app_dt_image_open(const char *name, size_t *size);
int32_t app_dt_image_read_at(const char *name, size_t offset, uint8_t *buf,
                             size_t len, size_t *bytes_read);
/** Release the lock taken by app_dt_image_open(). */
void    app_dt_image_close(const char *name);

#ifdef __cplusplus
}
#endif
