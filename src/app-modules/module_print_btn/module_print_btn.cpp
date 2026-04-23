// module_print_btn.cpp
//
// ModulePrintBtn — Print 1 and Print 2 button driver.
//
// GPIO ISR contract:
//   Each button pin (34, 35) gets its own ISR registered with ANYEDGE.
//   On each edge the ISR reads the current pin level, then calls either
//   hsys_button_press_event() or hsys_button_release_event() on the
//   corresponding hsys_button_t instance.
//   hsys_button handles debounce and long-press timing internally using
//   hsys_soft_timer.

#include "module_print_btn.h"
#include "msg_printer_btn.h"
#include "pal_gpio.h"
#include "pal_time.h"
#include "pal_logger.h"

#define __TAG__    "PRNBTN  "
#define PRNBTN_LOG true

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModulePrintBtn s_instance;
ModulePrintBtn *ModulePrintBtn::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModulePrintBtn::init()
{
    // Initialise both hsys_button instances (debounce=50ms, long_press=5000ms)
    if (!hsys_button_init(&_button_p1, _on_p1_short_press, _on_p1_long_press, 50, 5000))
    {
        LOG_MSG_ERROR(PRNBTN_LOG, "hsys_button_init failed for P1");
    }

    if (!hsys_button_init(&_button_p2, _on_p2_short_press, _on_p2_long_press, 50, 5000))
    {
        LOG_MSG_ERROR(PRNBTN_LOG, "hsys_button_init failed for P2");
    }

    // GPIO config — input-only pins, any-edge interrupt
    pal_gpio_config_t cfg_p1 = {
        .dir        = PAL_GPIO_DIR_INPUT,
        .pull       = PAL_GPIO_PULL_NONE,   // GPIO34 is input-only
        .open_drain = false,
        .drive      = PAL_GPIO_DRIVE_DEFAULT,
        .intr_type  = PAL_GPIO_INTR_ANYEDGE,
        .isr_callback = _gpio_isr_p1,
        .isr_arg      = &_button_p1,
    };
    pal_gpio_config(PRINT1_BTN_GPIO, cfg_p1);

    pal_gpio_config_t cfg_p2 = {
        .dir        = PAL_GPIO_DIR_INPUT,
        .pull       = PAL_GPIO_PULL_NONE,   // GPIO35 is input-only
        .open_drain = false,
        .drive      = PAL_GPIO_DRIVE_DEFAULT,
        .intr_type  = PAL_GPIO_INTR_ANYEDGE,
        .isr_callback = _gpio_isr_p2,
        .isr_arg      = &_button_p2,
    };
    pal_gpio_config(PRINT2_BTN_GPIO, cfg_p2);

    LOG_MSG_INFO(PRNBTN_LOG, "init — GPIO %d (P1) and GPIO %d (P2) armed",
                 PRINT1_BTN_GPIO, PRINT2_BTN_GPIO);
}

void ModulePrintBtn::on_msg_received(const hsys_msg_t & /*msg*/)
{
    // No subscriptions — all work is interrupt-driven
}

// ── Static GPIO ISRs ──────────────────────────────────────────────────────────

void ModulePrintBtn::_gpio_isr_p1(pal_gpio_num_t gpio, void *arg)
{
    pal_gpio_level_t level = PAL_GPIO_LEVEL_LOW;
    pal_gpio_get_level(gpio, &level);
    uint64_t ts = pal_time_get_us_from_isr();

    if (level == PAL_GPIO_LEVEL_HIGH)
        hsys_button_press_event(arg, ts);
    else
        hsys_button_release_event(arg, ts);
}

void ModulePrintBtn::_gpio_isr_p2(pal_gpio_num_t gpio, void *arg)
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

void ModulePrintBtn::_on_p1_short_press()
{
    LOG_MSG_INFO(PRNBTN_LOG, "Print1 short press");
    MsgPrinterBtn::Payload p{ .button_id = MsgPrinterBtn::BTN_ID_PRINT1,
                              .status    = BTN_SHORT_PRESS };
    hsys_msg_t *msg = MsgPrinterBtn::create(s_instance.id(), p);
    if (msg) s_instance.publish(msg);
}

void ModulePrintBtn::_on_p1_long_press()
{
    LOG_MSG_INFO(PRNBTN_LOG, "Print1 long press");
    MsgPrinterBtn::Payload p{ .button_id = MsgPrinterBtn::BTN_ID_PRINT1,
                              .status    = BTN_LONG_PRESS };
    hsys_msg_t *msg = MsgPrinterBtn::create(s_instance.id(), p);
    if (msg) s_instance.publish(msg);
}

void ModulePrintBtn::_on_p2_short_press()
{
    LOG_MSG_INFO(PRNBTN_LOG, "Print2 short press");
    MsgPrinterBtn::Payload p{ .button_id = MsgPrinterBtn::BTN_ID_PRINT2,
                              .status    = BTN_SHORT_PRESS };
    hsys_msg_t *msg = MsgPrinterBtn::create(s_instance.id(), p);
    if (msg) s_instance.publish(msg);
}

void ModulePrintBtn::_on_p2_long_press()
{
    LOG_MSG_INFO(PRNBTN_LOG, "Print2 long press");
    MsgPrinterBtn::Payload p{ .button_id = MsgPrinterBtn::BTN_ID_PRINT2,
                              .status    = BTN_LONG_PRESS };
    hsys_msg_t *msg = MsgPrinterBtn::create(s_instance.id(), p);
    if (msg) s_instance.publish(msg);
}
