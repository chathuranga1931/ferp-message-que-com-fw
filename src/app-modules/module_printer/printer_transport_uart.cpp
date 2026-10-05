// printer_transport_uart.cpp

#include "printer_transport_uart.h"
#include "pal_types.h"
#include "pal_logger.h"
#include <stdio.h>

#define __TAG__   "PRN_UART"
#define PTU_LOG   true

bool PrinterTransportUart::begin(uint32_t baud)
{
    if (baud == 0) baud = 9600;
    if (_ready && baud == _baud) return true;

    if (_ready) {
        pal_uart_deinit(_port);
        _ready = false;
    }

    pal_uart_config_t cfg = {};
    cfg.port           = _port;
    cfg.baud_rate      = baud;
    cfg.data_bits      = PAL_UART_DATA_8_BITS;
    cfg.parity         = PAL_UART_PARITY_DISABLE;
    cfg.stop_bits      = PAL_UART_STOP_BITS_1;
    cfg.flow_ctrl      = PAL_UART_HW_FLOWCTRL_DISABLE;
    cfg.tx_pin         = _tx;
    cfg.rx_pin         = _rx;
    cfg.rts_pin        = -1;
    cfg.cts_pin        = -1;
    cfg.rx_buffer_size = 256;   // must exceed the HW FIFO; RX is unused
    cfg.tx_buffer_size = 0;     // blocking writes: write() returns once bytes are in the FIFO

    if (pal_uart_init(&cfg) != PAL_OK) {
        LOG_MSG_ERROR(PTU_LOG, "UART%d init failed (tx=%ld rx=%ld baud=%lu)",
                      (int)_port, (long)_tx, (long)_rx, (unsigned long)baud);
        return false;
    }
    _baud  = baud;
    _ready = true;
    LOG_MSG_INFO(PTU_LOG, "UART%d ready — tx=%ld rx=%ld baud=%lu",
                 (int)_port, (long)_tx, (long)_rx, (unsigned long)baud);
    return true;
}

bool PrinterTransportUart::write(const uint8_t *data, size_t len, uint32_t /*timeout_ms*/)
{
    if (!_ready || !data) return false;
    size_t off = 0;
    while (off < len) {
        int32_t n = pal_uart_write(_port, data + off, len - off);
        if (n <= 0) {
            LOG_MSG_ERROR(PTU_LOG, "UART write failed at %u/%u", (unsigned)off, (unsigned)len);
            return false;
        }
        off += (size_t)n;
    }
    pal_uart_flush_tx(_port);   // wait until the last byte has left the shifter
    return true;
}

void PrinterTransportUart::detail(char *out, size_t out_len) const
{
    snprintf(out, out_len, "UART%d %lu baud", (int)_port, (unsigned long)_baud);
}
