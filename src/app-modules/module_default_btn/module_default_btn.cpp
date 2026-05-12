// module_default_btn.cpp
//
// ModuleDefaultBtn — default (factory/config) button driver.
//
// GPIO ISR contract:
//   pal_gpio_config() registers _gpio_isr with PAL_GPIO_INTR_ANYEDGE.
//   On each edge the ISR reads the current pin level, then calls either
//   hsys_button_press_event() or hsys_button_release_event().
//   hsys_button handles all debounce and long-press timing internally using
//   hsys_soft_timer (no FreeRTOS dependency — works on macOS too).

#include "module_default_btn.h"
#include "msg_default_btn.h"
#include "pal_gpio.h"
#include "pal_time.h"
#include "pal_logger.h"
#include "app_hw_config.h"

#define __TAG__      "DEFBTN  "
#define DEFBTN_LOG   true

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleDefaultBtn s_instance;
ModuleDefaultBtn *ModuleDefaultBtn::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleDefaultBtn::init()
{
    // Initialise hsys_button (debounce=200ms, long_press=5000ms)
    if (!hsys_button_init(&_button,
                          _on_short_press,
                          _on_long_press,
                          200,
                          5000))
    {
        LOG_MSG_ERROR(DEFBTN_LOG, "hsys_button_init failed");
    }

    // Configure GPIO 36 as input — any-edge interrupt → _gpio_isr
    pal_gpio_config_t cfg = {
        .dir        = PAL_GPIO_DIR_INPUT,
        .pull       = PAL_GPIO_PULL_NONE,  // GPIO36 is input-only, no internal pull
        .open_drain = false,
        .drive      = PAL_GPIO_DRIVE_DEFAULT,
        .intr_type  = PAL_GPIO_INTR_ANYEDGE,
        .isr_callback = _gpio_isr,
        .isr_arg      = &_button,
    };
    pal_gpio_config(DEFAULT_BTN_GPIO, cfg);

    LOG_MSG_INFO(DEFBTN_LOG, "init — GPIO %d armed (any-edge ISR)", DEFAULT_BTN_GPIO);
}

void ModuleDefaultBtn::on_msg_received(const hsys_msg_t & /*msg*/)
{
    // No subscriptions — all work is interrupt-driven
}

// ── Static GPIO ISR ────────────────────────────────────────────────────────────

void ModuleDefaultBtn::_gpio_isr(pal_gpio_num_t gpio, void *arg)
{
    pal_gpio_level_t level = PAL_GPIO_LEVEL_LOW;
    pal_gpio_get_level(gpio, &level);
    uint64_t ts = pal_time_get_us_from_isr();

    if (level == PAL_GPIO_LEVEL_HIGH)
        hsys_button_press_event(arg, ts);
    else
        hsys_button_release_event(arg, ts);
}

// ── Static hsys_button callbacks ──────────────────────────────────────────────

void ModuleDefaultBtn::_on_short_press()
{
    LOG_MSG_INFO(DEFBTN_LOG, "short press");
    MsgDefaultBtn::Payload p{};
    p.status = BTN_SHORT_PRESS;
    hsys_msg_t *msg = MsgDefaultBtn::create(s_instance.id(), p);
    if (msg) s_instance.publish(msg);
}

void ModuleDefaultBtn::_on_long_press()
{
    LOG_MSG_INFO(DEFBTN_LOG, "long press");
    MsgDefaultBtn::Payload p{};
    p.status = BTN_LONG_PRESS;
    hsys_msg_t *msg = MsgDefaultBtn::create(s_instance.id(), p);
    if (msg) s_instance.publish(msg);
}
