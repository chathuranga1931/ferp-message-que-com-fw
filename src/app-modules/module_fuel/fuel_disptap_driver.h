// fuel_disptap_driver.h
//
// FuelDispTapDriver — owns the comms layer between the ESP32 host and the
// DT board (dispenser tap display) over UART.
//
// In the real (ESP-IDF) build it calls the C API in com_distap.h / cmd_distap.h
// from the ferp-device-firmware submodule.  In the simulator build those same
// symbols are provided by mac_com_distap.cpp / mac_cmd_distap.cpp, which are
// no-ops backed by the TCP bridge — so this file contains NO #ifdef guards.
// The difference is resolved entirely by the linker.
//
// Lifecycle:
//   start(type, cb)  — init comms, register frame callbacks
//   stop()           — suspend comms
//
// Frame callback:
//   void cb(uint8_t nozzle_idx, display_type_t type, const uint8_t *raw_data)
//   raw_data points to a display_data_t struct (cast to access fields).

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "fuel_pump_types.h"   // display_type_t, display_data_t
#include "com_distap.h"        // raw_capture_chunk_t

// DT_TYPE config values — the DisplayTap board variant.  The chip family is
// fixed by the product build; only the board variant is configured.
enum : uint32_t {
    DT_TYPE_ESP07_A = 1,   ///< ESP07 DT on the 2308-modified board (inverted reset, no default button)
    DT_TYPE_ESP07_B = 2,   ///< ESP07 DT on the 2404 board
    DT_TYPE_ESP32_A = 3,   ///< ESP32 DT on the 2602 board
};

class FuelDispTapDriver
{
public:
    // Raw C function pointer — must be a static/free function so it can be
    // stored as a function pointer and called from a C-style ISR / callback.
    using frame_cb_t = void (*)(uint8_t nozzle_idx, display_type_t type,
                                const uint8_t *raw_data);

    FuelDispTapDriver()  = default;
    ~FuelDispTapDriver() = default;

    /**
     * Start the comms layer.
     *
     * @param display_type    The type of display protocol on the DT board.
     * @param on_frame        Callback invoked for each decoded data frame.
     *                        Called from the UART RX ISR (real) or the TCP
     *                        read thread (simulator).  Must be ISR-safe /
     *                        thread-safe.
     * @param version_out     Optional buffer to receive the post-flash DT board
     *                        firmware version string (null-terminated).
     * @param version_out_len Size of version_out buffer in bytes.
     */
    void start(display_type_t display_type, frame_cb_t on_frame,
               char *version_out = nullptr, size_t version_out_len = 0);

    /** Suspend comms (called on module stop or reconfiguration). */
    void stop();

    /**
     * Apply the configured DT_TYPE: board variant (DT reset polarity, default
     * button).  Call once, before start().  The value is not validated here —
     * a wrong one leaves the DT board unanswered, reported as DT status ERROR.
     *
     * @return the DT_TYPE applied
     */
    static uint32_t apply_dt_type(uint32_t configured);

private:
    // Static storage — one singleton driver per firmware image is enough.
    static frame_cb_t     _on_frame_cb;
    static display_type_t _active_type;

    // C-linkage callbacks passed into init_comms_distap().
    // Route to the stored frame_cb_t with the nozzle index injected.
    static void _dis1_event(display_type_t type, uint8_t *data);
    static void _dis2_event(display_type_t type, uint8_t *data);

    // Raw-capture callbacks (display types >= DIS_RAW_TYPE_BASE only) —
    // logging only, deliberately self-contained here rather than routed
    // through frame_cb_t: raw chunks are not display_data_t-shaped and
    // must never reach ModuleFuel's fuel pipeline. One per physical
    // channel per data line (SDATA1/SDATA2 are independent streams, each
    // under its own pck_id — see com_distap.h).
    static void _dis1_l1_raw_event(const raw_capture_chunk_t *chunk);
    static void _dis1_l2_raw_event(const raw_capture_chunk_t *chunk);
    static void _dis2_l1_raw_event(const raw_capture_chunk_t *chunk);
    static void _dis2_l2_raw_event(const raw_capture_chunk_t *chunk);
};
