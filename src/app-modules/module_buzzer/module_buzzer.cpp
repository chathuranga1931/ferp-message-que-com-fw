// module_buzzer.cpp
//
// ModuleBuzzer — drives the piezo buzzer via hsys_buz.
//
// Pattern reference (CUE_RESOLUTION_MS = 100 ms per bit):
//   0b11111111, length=8, repeat=1  → 8 × 100 ms = 2 s solid tone (print press)
//   0b101,      length=3, repeat=1  → ON-OFF-ON × 100 ms = double blip (fuel pumped)

#include "module_buzzer.h"
#include "msg_printer_btn.h"
#include "msg_fuel_pumped.h"
#include "msg_config_ready.h"
#include "pal_gpio.h"
#include "pal_logger.h"
#include "app_hw_config.h"

#define __TAG__       "BUZZER  "
#define BUZ_LOG_EN    true

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleBuzzer s_instance;
ModuleBuzzer *ModuleBuzzer::instance() { return &s_instance; }

// ── Static GPIO callbacks used by hsys_buz ────────────────────────────────────

void ModuleBuzzer::_buz_on()  { pal_gpio_set_level(BUZ_GPIO, PAL_GPIO_LEVEL_HIGH); }
void ModuleBuzzer::_buz_off() { pal_gpio_set_level(BUZ_GPIO, PAL_GPIO_LEVEL_LOW);  }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleBuzzer::init()
{
    // Configure GPIO as output, initially silent
    pal_gpio_config_t cfg = PAL_GPIO_CFG_OUTPUT();
    pal_gpio_config(BUZ_GPIO, cfg);
    pal_gpio_set_level(BUZ_GPIO, PAL_GPIO_LEVEL_LOW);

    if (!hsys_buz_init(&_buz, _buz_on, _buz_off)) {
        LOG_MSG_ERROR(BUZ_LOG_EN, "hsys_buz_init failed");
        return;
    }

    subscribe(MsgPrinterBtn::ID);
    subscribe(MsgFuelPumped::ID);
    subscribe(MsgConfigReady::ID);

    LOG_MSG_INFO(BUZ_LOG_EN, "init — buzzer ready");
}

// ── Message handler ────────────────────────────────────────────────────────────

void ModuleBuzzer::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {

        case MsgPrinterBtn::ID: {
            auto p = MsgPrinterBtn::deserialize(msg);
            LOG_MSG_INFO(BUZ_LOG_EN, "MsgPrinterBtn — btn=%u status=%u",
                         p.button_id, (uint8_t)p.status);
            if (p.button_id == MsgPrinterBtn::BTN_ID_PRINT1 &&
                p.status    == BTN_SHORT_PRESS)
            {
                LOG_MSG_INFO(BUZ_LOG_EN, "print-1 short press — playing tone");
                _play_print_press();
            }
            break;
        }

        case MsgFuelPumped::ID: {
            auto p = MsgFuelPumped::deserialize(msg);
            if (p.vol_lx1000 > 0) {
                LOG_MSG_INFO(BUZ_LOG_EN, "fuel pumped (nozzle %u) — playing double blip",
                             p.nozzle_idx);
                _play_fuel_pumped();
            }
            break;
        }

        case MsgConfigReady::ID:
            LOG_MSG_INFO(BUZ_LOG_EN, "config ready — playing double beep");
            _play_config_ready();
            break;

        default:
            break;
    }
}

// ── Private helpers ────────────────────────────────────────────────────────────

void ModuleBuzzer::_play_print_press()
{
    // 8 × 100 ms = 2 s solid tone, play once
    hsys_buz_stop(&_buz);
    hsys_buz_set_pattern(&_buz, 0b1100, 4, 1);
    hsys_buz_start(&_buz);
}

void ModuleBuzzer::_play_fuel_pumped()
{
    // ON-OFF-ON × 100 ms = double blip, play once
    hsys_buz_stop(&_buz);
    hsys_buz_set_pattern(&_buz, 0b1010, 4, 1);
    hsys_buz_start(&_buz);
}

void ModuleBuzzer::_play_config_ready()
{
    // ON-OFF-ON-OFF × 100 ms = two short beeps, play once
    hsys_buz_stop(&_buz);
    hsys_buz_set_pattern(&_buz, 0b1010, 4, 1);
    hsys_buz_start(&_buz);
}
