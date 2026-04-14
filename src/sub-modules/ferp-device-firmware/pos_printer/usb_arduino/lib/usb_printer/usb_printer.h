#include "stdlib.h"

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    void usb_printer_init();
    bool usb_printer_connected();
    void usb_printer_send_data(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

