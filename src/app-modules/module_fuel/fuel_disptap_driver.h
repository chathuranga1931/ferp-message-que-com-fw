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
#include "fuel_pump_types.h"   // display_type_t, display_data_t

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

private:
    // Static storage — one singleton driver per firmware image is enough.
    static frame_cb_t     _on_frame_cb;
    static display_type_t _active_type;

    // C-linkage callbacks passed into init_comms_distap().
    // Route to the stored frame_cb_t with the nozzle index injected.
    static void _dis1_event(display_type_t type, uint8_t *data);
    static void _dis2_event(display_type_t type, uint8_t *data);
};
