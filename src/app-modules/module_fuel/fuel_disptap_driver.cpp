// fuel_disptap_driver.cpp

#include "fuel_disptap_driver.h"
#include "com_distap.h"    // init_comms_distap(), suspend_comms_distap()
#include "cmd_distap.h"    // distap_get_fw_version(), distap_set_display_type(), distap_set_err_mask()
#ifndef FERP_SIMULATOR
#include "serial_flasher.h"
#include "app_dt_image.h"   // APP_DT_FLASH_NVS_NS
#include "pal_nvs.h"
#endif
#include "pal_logger.h"
#include <stdio.h>
#include <string.h>
#include "pal_time.h"

#define __TAG__      "DISTAP  "
#define DISTAP_LOG   true

#define MLOG(fmt, ...)  LOG_MSG_INFO( DISTAP_LOG, fmt, ##__VA_ARGS__)
#define MLOGE(fmt, ...) LOG_MSG_ERROR(DISTAP_LOG, fmt, ##__VA_ARGS__)

// ---------------------------------------------------------------------------
// Static state (one driver instance per firmware)
// ---------------------------------------------------------------------------

FuelDispTapDriver::frame_cb_t     FuelDispTapDriver::_on_frame_cb  = nullptr;
display_type_t FuelDispTapDriver::_active_type  = DIS_NONE;

// ---------------------------------------------------------------------------
// C-linkage event callbacks forwarded to the stored frame_cb_t
// ---------------------------------------------------------------------------

void FuelDispTapDriver::_dis1_event(display_type_t type, uint8_t *data)
{
    const display_data_t *dis = (const display_data_t *)data;
    // MLOG("DIS1 event: type=%d, Vol=%.03f, Unit=%.02f, Tot=%.02f", 
    //     (int)type, dis->volume_l/1000.0, dis->unit_price/100.0, dis->total_price/100.0);
    if (_on_frame_cb) _on_frame_cb(0, type, data);
}

void FuelDispTapDriver::_dis2_event(display_type_t type, uint8_t *data)
{
    const display_data_t *dis = (const display_data_t *)data;
    // MLOG("DIS2 event: type=%d, Vol=%.03f, Unit=%.02f, Tot=%.02f",
    //     (int)type, dis->volume_l/1000.0, dis->unit_price/100.0, dis->total_price/100.0);
    if (_on_frame_cb) _on_frame_cb(1, type, data);
}

// ---------------------------------------------------------------------------
// Raw-capture callbacks (display types >= DIS_RAW_TYPE_BASE) — logging only.
// Never touches ModuleFuel/_on_frame_cb: raw chunks aren't display_data_t
// shaped, so they must never enter the fuel pipeline.
// ---------------------------------------------------------------------------

static void _log_raw_chunk(uint8_t nozzle_idx, uint8_t data_line, const raw_capture_chunk_t *chunk)
{
    char hex[3 * MAX_DATA + 1] = {};
    size_t pos = 0;
    for (uint8_t i = 0; i < chunk->chunk_len && pos + 3 < sizeof(hex); i++) {
        pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", chunk->data[i]);
    }
    MLOG("RAW nozzle=%u line=%u bits=%u chunk=%u/%u total=%uB: %s",
         (unsigned)nozzle_idx + 1, (unsigned)data_line, (unsigned)chunk->codeword_bits,
         (unsigned)chunk->chunk_index + 1, (unsigned)chunk->chunk_count,
         (unsigned)chunk->total_len, hex);
}

void FuelDispTapDriver::_dis1_l1_raw_event(const raw_capture_chunk_t *chunk)
{
    _log_raw_chunk(0, 1, chunk);
}

void FuelDispTapDriver::_dis1_l2_raw_event(const raw_capture_chunk_t *chunk)
{
    _log_raw_chunk(0, 2, chunk);
}

void FuelDispTapDriver::_dis2_l1_raw_event(const raw_capture_chunk_t *chunk)
{
    _log_raw_chunk(1, 1, chunk);
}

void FuelDispTapDriver::_dis2_l2_raw_event(const raw_capture_chunk_t *chunk)
{
    _log_raw_chunk(1, 2, chunk);
}

#ifndef FERP_SIMULATOR
// ---------------------------------------------------------------------------
// DT firmware update from the stored image (SD card).  The DT is only flashed when
// its version differs from the stored image, the result is checked by reading the
// version back, and one image version is tried at most DT_FLASH_MAX_ATTEMPTS times
// (counted in NVS before each attempt, so a crash mid-flash counts too).  Without the
// limit a DT that never takes the image is re-flashed at every boot (~35 s without
// display data each time).  Storing a new image resets the count (app_dt_image.cpp).
// ---------------------------------------------------------------------------
#define DT_FLASH_MAX_ATTEMPTS 3

static void _update_dt_firmware(const char *dt_version)
{
    char image[32] = {};
    if (serial_flash_image_version(image, sizeof(image)) != ESP_OK) {
        MLOG("DT update: no valid DT image stored");
        return;
    }
    if (dt_version && strcmp(dt_version, image) == 0) {
        MLOG("DT update: DT already runs the stored image %s", image);
        pal_nvs_erase_key(APP_DT_FLASH_NVS_NS, "ver");
        pal_nvs_erase_key(APP_DT_FLASH_NVS_NS, "cnt");
        return;
    }

    char tried[32] = {};
    int64_t count = 0;
    size_t n = 0;
    if (pal_nvs_read_blob(APP_DT_FLASH_NVS_NS, "ver", tried, sizeof(tried) - 1, &n) == 0 && strcmp(tried, image) == 0) {
        pal_nvs_read_i64(APP_DT_FLASH_NVS_NS, "cnt", &count);
    }
    if (count >= DT_FLASH_MAX_ATTEMPTS) {
        MLOGE("DT update: image %s not applied after %d attempts (DT reports %s) - not flashing again until a new image is stored",
              image, (int)count, dt_version ? dt_version : "nothing");
        return;
    }
    pal_nvs_write_blob(APP_DT_FLASH_NVS_NS, "ver", image, strlen(image) + 1);
    pal_nvs_write_i64(APP_DT_FLASH_NVS_NS, "cnt", count + 1);

    MLOG("DT update: %s -> %s (attempt %d of %d)", dt_version ? dt_version : "?", image, (int)count + 1, DT_FLASH_MAX_ATTEMPTS);
    const serial_flash_result_t r = serial_flash_update(true);    // versions compared above

    char now[32] = {};
    if (distap_get_fw_version(now) == ESP_OK && strcmp(now, image) == 0) {
        MLOG("DT update: done, DT runs %s", now);
        pal_nvs_erase_key(APP_DT_FLASH_NVS_NS, "ver");
        pal_nvs_erase_key(APP_DT_FLASH_NVS_NS, "cnt");
    } else {
        MLOGE("DT update: image %s did not apply (flash result %d, DT reports %s)", image, (int)r, now[0] ? now : "nothing");
    }
}
#endif  // FERP_SIMULATOR

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

#include "board.h"
void FuelDispTapDriver::start(display_type_t display_type, frame_cb_t on_frame,
                              char *version_out, size_t version_out_len)
{
    _active_type  = display_type;
    _on_frame_cb  = on_frame;

    // ── Start UART comms receive task FIRST — must be running before any
    //    distap_* command is issued, otherwise responses time out (no reader). ──
    esp_err_t rc = init_comms_distap(_dis1_event, _dis2_event,
                                      _dis1_l1_raw_event, _dis1_l2_raw_event,
                                      _dis2_l1_raw_event, _dis2_l2_raw_event);
    if (rc != ESP_OK) {
        MLOGE("init_comms_distap failed (%d)", rc);
        return;
    }

    // ── Release reset AFTER comms are ready, then wait for device to boot ──
    MLOG("Starting Display Tap, Enable");
#ifndef FERP_SIMULATOR
    gpio_set_reset_distap(false);
    pal_time_delay_ms(500);
#endif

    // ── Version check ─────────────────────────────────────────────────────────
    char version[32] = {};
    rc = distap_get_fw_version(version);
    // A DT that is still restarting (e.g. after a type change) answers late: retry before
    // treating it as broken - an unanswered version read makes the DT update re-flash it.
    for (int i = 0; i < 2 && rc != ESP_OK; i++) {
        pal_time_delay_ms(2000);
        rc = distap_get_fw_version(version);
    }
    const int rc_version = rc;
    if (rc == ESP_OK) 
    {
        MLOG("DT board FW version: %s", version);
    } 
    else 
    {
        MLOGE("distap_get_fw_version failed (%d)", rc);
    }

    // ── Set display type ──────────────────────────────────────────────────────
    const int dt_type_before = distap_last_display_type();
    rc = distap_set_display_type(display_type);
    if (rc != ESP_OK && dt_type_before == (int)display_type) {
        MLOGE("distap_set_display_type(%d) failed (%d)", (int)display_type, rc);
    }
#ifndef FERP_SIMULATOR
    if (dt_type_before != (int)display_type) {
        // The DT applies a new type by saving it to its flash and restarting itself in software.
        // 1) Wait until it reports the new type - never reset it while it may still be saving
        //    (an interrupted save corrupts its settings file; the ESP8266 then reformats storage).
        char v[32] = {};
        bool applied = false;
        // the ESP07 can need several seconds (save to flash, restart, mount its storage)
        for (int i = 0; i < 12 && !applied; i++) {
            pal_time_delay_ms(1000);
            applied = distap_get_fw_version(v) == ESP_OK && distap_last_display_type() == (int)display_type;
        }
        if (!applied) {
            MLOGE("display type %d: not applied by the DT (it reports %d) - not supported by this DT?",
                  (int)display_type, distap_last_display_type());
        } else {
            // 2) Then a hardware reset: after the software restart the capture can stay broken until
            //    the next hardware reset (seen: Wayne-6 on the ESP07 DT, Sanki on the ESP32 DT).
            gpio_set_reset_distap(true);
            pal_time_delay_ms(50);
            gpio_set_reset_distap(false);
            pal_time_delay_ms(800);                 // DT boot
            if (distap_get_fw_version(v) == ESP_OK && distap_last_display_type() == (int)display_type) {
                MLOG("display type %d -> %d: applied (DT hardware reset)", dt_type_before, (int)display_type);
            } else {
                MLOGE("display type %d: DT reports %d after the hardware reset", (int)display_type, distap_last_display_type());
            }
        }
    }
#endif

#ifndef FERP_SIMULATOR
    _update_dt_firmware(rc_version == ESP_OK ? version : nullptr);
#else
    // Serial flasher not available on simulator — skip DT board firmware update.
#endif  // FERP_SIMULATOR

    char disptap_version[50] = {0};
    distap_get_fw_version((char *)(disptap_version));
    MLOG("Distap version: %s", disptap_version);
    if (version_out && version_out_len > 0) {
        strncpy(version_out, disptap_version, version_out_len - 1);
        version_out[version_out_len - 1] = '\0';
    }
    
    distap_get_fw_name();
    distap_get_fw_timedate();

    distap_set_display_type(display_type);
    
    // ── Set error mask ────────────────────────────────────────────────────────
    // price_gap is excluded: when the 7-digit display rolls over at Rs.99999.99
    // the gap spikes to ~10,000,000 which the DT board would otherwise suppress.
    // censtar7_process_data() on this side corrects the rollover instead.
    data_error_t err_mask{};
    err_mask.u8int = 0xFF;
    err_mask.bits.price_gap = false;
    rc = distap_set_err_mask(err_mask);
    if (rc != ESP_OK) {
        MLOGE("distap_set_err_mask failed (%d)", rc);
    }

    MLOG("started  display_type=%d", (int)display_type);
}

void FuelDispTapDriver::stop()
{
    suspend_comms_distap();
    _on_frame_cb = nullptr;
    MLOG("stopped");
}
