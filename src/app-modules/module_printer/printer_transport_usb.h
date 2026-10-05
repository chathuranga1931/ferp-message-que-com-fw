// printer_transport_usb.h
//
// PrinterTransportUsb — ESC/POS over USB (ESP32-S3 USB OTG in host mode).
//
// Based on old/ferp-printer-usb-no-test (usb_printer.c) with these changes:
//   * the interface and bulk-OUT endpoint are discovered from the config
//     descriptor (printer class 0x07 preferred) instead of hard-coding
//     interface 0 / endpoint 0x01 — those remain the fallback;
//   * transfers are allocated once and waited for (the example freed the
//     transfer straight after submit, i.e. while it was still in flight);
//   * data is split into chunks without 0xAA padding (the Arduino variant
//     padded to the packet size, which printed junk characters);
//   * hot-plug: the printer can be unplugged / re-plugged at any time.
//
// Requires the espressif/usb component (ESP-IDF v6 moved the USB Host
// library out of the IDF tree).

#pragma once

#include "printer_transport.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"

class PrinterTransportUsb : public IPrinterTransport
{
public:
    static constexpr size_t   XFER_SIZE        = 1024;   ///< bytes per bulk transfer
    static constexpr uint8_t  FALLBACK_INTF    = 0;
    static constexpr uint8_t  FALLBACK_EP_OUT  = 0x01;

    bool begin(uint32_t baud) override;          // baud ignored
    bool is_ready() const override { return _ready; }
    bool write(const uint8_t *data, size_t len, uint32_t timeout_ms) override;
    const char *name() const override { return "usb"; }
    void detail(char *out, size_t out_len) const override;

private:
    enum : uint32_t { ACT_OPEN = 1u << 0, ACT_CLOSE = 1u << 1 };

    bool                     _started   = false;
    volatile bool            _ready     = false;
    volatile bool            _in_flight = false;
    volatile uint32_t        _actions   = 0;
    usb_host_client_handle_t _client    = nullptr;
    usb_device_handle_t      _dev       = nullptr;
    uint8_t                  _addr      = 0;
    uint8_t                  _intf      = FALLBACK_INTF;
    uint8_t                  _ep_out    = FALLBACK_EP_OUT;
    bool                     _claimed   = false;
    usb_transfer_t          *_xfer      = nullptr;
    SemaphoreHandle_t        _done      = nullptr;   ///< given by the transfer callback
    SemaphoreHandle_t        _lock      = nullptr;   ///< serialises write() vs. close
    SemaphoreHandle_t        _installed = nullptr;   ///< daemon → client: host lib installed
    volatile int             _xfer_status = 0;
    volatile size_t          _xfer_actual = 0;
    char                     _product[40] = {};

    static void _daemon_task(void *arg);
    static void _client_task(void *arg);
    static void _client_event_cb(const usb_host_client_event_msg_t *ev, void *arg);
    static void _transfer_cb(usb_transfer_t *t);

    void _open_device();
    bool _close_device();          ///< false → retry later (transfer still in flight)
    void _find_bulk_out(const usb_config_desc_t *cfg);
};
