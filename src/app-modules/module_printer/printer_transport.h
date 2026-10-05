// printer_transport.h
//
// IPrinterTransport — byte pipe from ModulePrinter to the physical printer.
//
// Every printer speaks the same ESC/POS protocol; only the link differs:
//   PrinterTransportUart  — ESP32 UART (ferp-printer-com-v1)
//   PrinterTransportUsb   — ESP32-S3 USB host, bulk OUT endpoint (ferp-printer-usb-v1)
//
// The product's app.cpp picks one instance and hands it to ModulePrinter via
// ModulePrinter::set_transport() before the framework starts.
// All calls are made from the printer task (ModulePrinter's HSYS task).

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

class IPrinterTransport
{
public:
    virtual ~IPrinterTransport() = default;

    /** Bring the link up.  Called once config is loaded and again when the
     *  baud rate changes (UART).  @param baud  printer baud rate (UART only). */
    virtual bool begin(uint32_t baud) = 0;

    /** True when a write is expected to succeed (USB: printer attached). */
    virtual bool is_ready() const = 0;

    /** Write all bytes or fail.  Blocks up to timeout_ms. */
    virtual bool write(const uint8_t *data, size_t len, uint32_t timeout_ms) = 0;

    /** Short transport name for status reports ("uart" / "usb"). */
    virtual const char *name() const = 0;

    /** Human-readable detail, e.g. "UART2 9600" or the USB product string. */
    virtual void detail(char *out, size_t out_len) const = 0;
};
