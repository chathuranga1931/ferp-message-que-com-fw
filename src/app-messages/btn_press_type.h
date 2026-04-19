// btn_press_type.h
//
// Shared button press type used by MsgDefaultBtn and MsgPrinterBtn.

#ifndef BTN_PRESS_TYPE_H
#define BTN_PRESS_TYPE_H

#include <stdint.h>

typedef enum : uint8_t {
    BTN_SHORT_PRESS = 0,   ///< Press duration < long_press_duration_ms
    BTN_LONG_PRESS  = 1,   ///< Press duration >= long_press_duration_ms
} btn_press_t;

#endif // BTN_PRESS_TYPE_H
