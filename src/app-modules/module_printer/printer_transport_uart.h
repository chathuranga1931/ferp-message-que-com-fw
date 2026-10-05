// printer_transport_uart.h
//
// PrinterTransportUart — ESC/POS over a PAL UART (TX only; RX pin is
// configured but unused, the legacy printer never read status back).

#pragma once

#include "printer_transport.h"
#include "pal_uart.h"

class PrinterTransportUart : public IPrinterTransport
{
public:
    PrinterTransportUart(pal_uart_port_t port, int32_t tx_pin, int32_t rx_pin)
        : _port(port), _tx(tx_pin), _rx(rx_pin) {}

    bool begin(uint32_t baud) override;
    bool is_ready() const override { return _ready; }
    bool write(const uint8_t *data, size_t len, uint32_t timeout_ms) override;
    const char *name() const override { return "uart"; }
    void detail(char *out, size_t out_len) const override;

private:
    pal_uart_port_t _port;
    int32_t         _tx;
    int32_t         _rx;
    uint32_t        _baud  = 0;
    bool            _ready = false;
};
