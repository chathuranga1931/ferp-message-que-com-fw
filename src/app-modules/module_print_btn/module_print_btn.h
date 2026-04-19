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
#include "pal_gpio.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#define MODULE_PRINT_BTN_ID    ((hsys_module_id_t)10)
#define MODULE_PRINT_BTN_NAME  "print_btn"

// GPIO pin assignments (board_2602: INPUT1 / PRINT1, INPUT2 / PRINT2)
#define PRINT1_BTN_GPIO  34
#define PRINT2_BTN_GPIO  35

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

private:
    hsys_button_t _button_p1{};
    hsys_button_t _button_p2{};

    // Static GPIO ISRs — bridge any-edge interrupt into the correct hsys_button
    static void _gpio_isr_p1(pal_gpio_num_t gpio, void *arg);
    static void _gpio_isr_p2(pal_gpio_num_t gpio, void *arg);

    // Static hsys_button callbacks — each pair references one button
    static void _on_p1_short_press();
    static void _on_p1_long_press();
    static void _on_p2_short_press();
    static void _on_p2_long_press();
};
