// module_leds.h
//
// ModuleLeds — drives LED1 and LED2 via the hsys_led peripheral.
//
// Behaviour:
//   - On init: initialises two hsys_led_t instances backed by pal_gpio.
//   - Subscribes to MSG_ID_SPIFFS_READY.
//   - Once SPIFFS is ready: sets a 250 ms blink pattern and starts both LEDs.
//
// hsys_led uses hsys_soft_timer internally — no ModuleTimer dependency.

#pragma once

#include "hsys_module.h"
#include "hsys_led.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_LEDS_NAME  "leds"

// GPIO pin assignments (matches board_2602 aliases / pal_mac_gpio pin table)
#define LED1_GPIO  5
#define LED2_GPIO  4

// ---------------------------------------------------------------------------
// ModuleLeds
// ---------------------------------------------------------------------------

class ModuleLeds : public HsysModule
{
public:
    ModuleLeds() : HsysModule(MODULE_LEDS_ID, MODULE_LEDS_NAME) {}

    static ModuleLeds *instance();

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    hsys_led_t _led1{};
    hsys_led_t _led2{};

    void _start_blink();

    // Static GPIO callbacks required by hsys_led_init()
    static void _led1_on();
    static void _led1_off();
    static void _led2_on();
    static void _led2_off();
};
