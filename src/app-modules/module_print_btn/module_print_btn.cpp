// module_print_btn.cpp
//
// ModulePrintBtn — Print 1 and Print 2 button driver.
//
// GPIO ISR contract:
//   Each button pin (34, 35) gets its own ISR registered with ANYEDGE.
//   On each edge the ISR pushes a _btn_event_t (with btn_idx) into
//   _event_queue and calls wake_from_isr().  on_wake() drains the queue on
//   the module task thread and dispatches to the correct hsys_button instance.
//   This keeps all hsys_button (soft timer / mutex) work off ISR context.

#include "module_print_btn.h"
#include "msg_printer_btn.h"
#include "hsys_queue.h"
#include "pal_gpio.h"
#include "pal_time.h"
#include "pal_logger.h"
#include "app_hw_config.h"

#define __TAG__    "PRNBTN  "
#define PRNBTN_LOG true

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModulePrintBtn s_instance;
ModulePrintBtn *ModulePrintBtn::instance() { return &s_instance; }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModulePrintBtn::init()
{
    if (!hsys_queue_init(&_event_queue, 5, sizeof(_btn_event_t)))
    {
        LOG_MSG_ERROR(PRNBTN_LOG, "event queue init failed");
    }

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
        .isr_arg      = nullptr,
    };
    pal_gpio_config(PRINT1_BTN_GPIO, cfg_p1);

    pal_gpio_config_t cfg_p2 = {
        .dir        = PAL_GPIO_DIR_INPUT,
        .pull       = PAL_GPIO_PULL_NONE,   // GPIO35 is input-only
        .open_drain = false,
        .drive      = PAL_GPIO_DRIVE_DEFAULT,
        .intr_type  = PAL_GPIO_INTR_ANYEDGE,
        .isr_callback = _gpio_isr_p2,
        .isr_arg      = nullptr,
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

void ModulePrintBtn::_gpio_isr_p1(pal_gpio_num_t gpio, void * /*arg*/)
{
    pal_gpio_level_t level = PAL_GPIO_LEVEL_LOW;
    pal_gpio_get_level(gpio, &level);
    _btn_event_t evt = {
        .btn_idx      = 0,
        .is_pressed   = (level == PAL_GPIO_LEVEL_HIGH),
        .timestamp_us = pal_time_get_us_from_isr(),
    };
    bool woken = false;
    hsys_queue_send_from_isr(&s_instance._event_queue, &evt, &woken);
    s_instance.wake_from_isr(&woken);
    if (woken) hsys_yield_from_isr();
}

void ModulePrintBtn::_gpio_isr_p2(pal_gpio_num_t gpio, void * /*arg*/)
{
    pal_gpio_level_t level = PAL_GPIO_LEVEL_LOW;
    pal_gpio_get_level(gpio, &level);
    _btn_event_t evt = {
        .btn_idx      = 1,
        .is_pressed   = (level == PAL_GPIO_LEVEL_HIGH),
        .timestamp_us = pal_time_get_us_from_isr(),
    };
    bool woken = false;
    hsys_queue_send_from_isr(&s_instance._event_queue, &evt, &woken);
    s_instance.wake_from_isr(&woken);
    if (woken) hsys_yield_from_isr();
}

// ── on_wake — drain queued button events on the module task thread ────────────

void ModulePrintBtn::on_wake()
{
    _btn_event_t evt;
    while (hsys_queue_receive(&_event_queue, &evt, 0)) {
        hsys_button_t *btn = (evt.btn_idx == 0) ? &_button_p1 : &_button_p2;
        if (evt.is_pressed)
            hsys_button_press_event(btn, evt.timestamp_us);
        else
            hsys_button_release_event(btn, evt.timestamp_us);
    }
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
