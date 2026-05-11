// fuel_disptap_driver.cpp

#include "fuel_disptap_driver.h"
#include "com_distap.h"    // init_comms_distap(), suspend_comms_distap()
#include "cmd_distap.h"    // distap_get_fw_version(), distap_set_display_type(), distap_set_err_mask()
#ifndef FERP_SIMULATOR
#include "serial_flasher.h"
#endif
#include "pal_logger.h"
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
    esp_err_t rc = init_comms_distap(_dis1_event, _dis2_event);
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
    if (rc == ESP_OK) 
    {
        MLOG("DT board FW version: %s", version);
    } 
    else 
    {
        MLOGE("distap_get_fw_version failed (%d)", rc);
    }

    // ── Set display type ──────────────────────────────────────────────────────
    rc = distap_set_display_type(display_type);
    if (rc != ESP_OK) {
        MLOGE("distap_set_display_type(%d) failed (%d)", (int)display_type, rc);
    }

#ifndef FERP_SIMULATOR
    start_serial_flash(false);
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
    
    // ── Set error mask (accept all errors for now) ────────────────────────────
    data_error_t err_mask{};
    err_mask.u8int = 0xFF;
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
