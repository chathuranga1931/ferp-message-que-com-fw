// module_buzzer.h
//
// ModuleBuzzer — drives the piezo buzzer via hsys_buz (soft-timer backed).
//
// Behaviour:
//   - On init: initialises hsys_buz_t with pal_gpio on/off callbacks.
//   - Subscribes to MsgPrinterBtn and MsgFuelPumped.
//   - Print-1 short press → solid 2 s tone  (0b11111111, 8, 1)
//   - Fuel transaction complete → double blip (0b101, 3, 1)
//
// hsys_buz uses hsys_soft_timer internally — no ModuleTimer dependency.

#pragma once

#include "hsys_module.h"
#include "hsys_buzzer.h"

// ---------------------------------------------------------------------------
// Module identity
// ---------------------------------------------------------------------------

#include "app_module_ids.h"
#define MODULE_BUZZER_NAME  "buzzer"

// GPIO pin assignment — OUTPUT2 on board_2602
#define BUZ_GPIO  26

// ---------------------------------------------------------------------------
// ModuleBuzzer
// ---------------------------------------------------------------------------

class ModuleBuzzer : public HsysModule
{
public:
    ModuleBuzzer() : HsysModule(MODULE_BUZZER_ID, MODULE_BUZZER_NAME) {}

    static ModuleBuzzer *instance();

protected:
    void init()                                  override;
    void on_msg_received(const hsys_msg_t &msg)  override;

private:
    hsys_buz_t _buz{};

    void _play_print_press();
    void _play_fuel_pumped();
    void _play_config_ready();

    // Static GPIO callbacks required by hsys_buz_init()
    static void _buz_on();
    static void _buz_off();
};
