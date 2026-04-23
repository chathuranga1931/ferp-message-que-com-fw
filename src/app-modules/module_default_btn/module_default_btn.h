// module_default_btn.h
//
// ModuleDefaultBtn — manages the single default (factory/config) button.
//
// Hardware:
//   GPIO 36 (INPUT5) — input-only pin, active-high via pal_gpio interrupt.
//
// Behaviour:
//   - Configures GPIO 36 as input with any-edge interrupt.
//   - The ISR bridges press/release events into hsys_button which handles
//     debounce (200 ms) and long-press detection (5000 ms).
//   - Publishes MsgDefaultBtn on each completed press (short or long).
//
// Debounce / timing (mirrors old app_default_btn):
//   short_press_duration_ms = 200
//   long_press_duration_ms  = 5000

#pragma once

#include "hsys_module.h"
#include "hsys_button.h"
#include "pal_gpio.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_DEFAULT_BTN_NAME  "default_btn"

// GPIO pin assignment (board_2602: INPUT5 / DEFAULT_BUTTON_GPIO_PIN)
#define DEFAULT_BTN_GPIO  36

// ---------------------------------------------------------------------------
// ModuleDefaultBtn
// ---------------------------------------------------------------------------

class ModuleDefaultBtn : public HsysModule
{
public:
    ModuleDefaultBtn() : HsysModule(MODULE_DEFAULT_BTN_ID, MODULE_DEFAULT_BTN_NAME) {}

    static ModuleDefaultBtn *instance();

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    hsys_button_t _button{};

    // Static GPIO ISR — bridges any-edge interrupt into hsys_button
    static void _gpio_isr(pal_gpio_num_t gpio, void *arg);

    // Static hsys_button callbacks (no args — reference singleton state)
    static void _on_short_press();
    static void _on_long_press();
};
