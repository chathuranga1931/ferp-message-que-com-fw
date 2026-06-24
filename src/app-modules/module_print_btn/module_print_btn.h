// module_print_btn.h
//
// ModulePrintBtn — manages the two printer buttons (Print 1 and Print 2).
//
// Hardware:
//   GPIO 34 (INPUT1) — Print 1 button, active-high via pal_gpio interrupt.
//   GPIO 35 (INPUT2) — Print 2 button, active-high via pal_gpio interrupt.
//
// Behaviour:
//   - Configures GPIO 34 and 35 as inputs with any-edge interrupt.
//   - Each button uses an independent hsys_button instance for debounce
//     (50 ms) and long-press detection (5000 ms).
//   - Publishes MsgPrinterBtn on each completed press with the button_id
//     and press type (short/long).
//
// Debounce / timing (mirrors old app_print_btn):
//   short_press_duration_ms = 50
//   long_press_duration_ms  = 5000

#pragma once

#include "hsys_module.h"
#include "hsys_button.h"
#include "hsys_queue.h"
#include "pal_gpio.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_PRINT_BTN_NAME  "print_btn"

// ---------------------------------------------------------------------------
// ModulePrintBtn
// ---------------------------------------------------------------------------

class ModulePrintBtn : public HsysModule
{
public:
    ModulePrintBtn() : HsysModule(MODULE_PRINT_BTN_ID, MODULE_PRINT_BTN_NAME) {}

    static ModulePrintBtn *instance();

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;
    void on_wake()                               override;

private:
    hsys_button_t       _button_p1{};
    hsys_button_t       _button_p2{};
    hsys_queue_handle_t _event_queue;

    struct _btn_event_t {
        uint8_t  btn_idx;       // 0 = P1, 1 = P2
        bool     is_pressed;
        uint64_t timestamp_us;
    };

    // Static GPIO ISRs — push press/release event to _event_queue + wake module
    static void _gpio_isr_p1(pal_gpio_num_t gpio, void *arg);
    static void _gpio_isr_p2(pal_gpio_num_t gpio, void *arg);

    // Static hsys_button callbacks — each pair references one button
    static void _on_p1_short_press();
    static void _on_p1_long_press();
    static void _on_p2_short_press();
    static void _on_p2_long_press();
};
