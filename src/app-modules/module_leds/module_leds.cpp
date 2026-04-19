// module_leds.cpp
//
// ModuleLeds — uses hsys_led (which drives hsys_soft_timer internally)
// to blink LED1 and LED2 at 250 ms once SPIFFS is ready.
//
// Pattern encoding (CUE_RESOLUTION_MS = 125 ms per step):
//   0b01, length=2 → step0=OFF(125ms), step1=ON(125ms), repeat=0xFF (forever)
//   Effective period: 250 ms  →  2 Hz blink

#include "module_leds.h"
#include "msg_spiffs_ready.h"
#include "pal_gpio.h"
#include "pal_logger.h"

#define __TAG__     "LEDS    "
#define LEDS_LOG_EN true

// ── Singleton ─────────────────────────────────────────────────────────────────

static ModuleLeds s_instance;
ModuleLeds *ModuleLeds::instance() { return &s_instance; }

// ── Static GPIO callbacks used by hsys_led ────────────────────────────────────

void ModuleLeds::_led1_on()  { pal_gpio_set_level(LED1_GPIO, PAL_GPIO_LEVEL_HIGH); }
void ModuleLeds::_led1_off() { pal_gpio_set_level(LED1_GPIO, PAL_GPIO_LEVEL_LOW);  }
void ModuleLeds::_led2_on()  { pal_gpio_set_level(LED2_GPIO, PAL_GPIO_LEVEL_HIGH); }
void ModuleLeds::_led2_off() { pal_gpio_set_level(LED2_GPIO, PAL_GPIO_LEVEL_LOW);  }

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ModuleLeds::init()
{
    // Configure GPIO pins as outputs, initially low
    pal_gpio_config_t cfg = PAL_GPIO_CFG_OUTPUT();
    pal_gpio_config(LED1_GPIO, cfg);
    pal_gpio_config(LED2_GPIO, cfg);

    pal_gpio_set_level(LED1_GPIO, PAL_GPIO_LEVEL_LOW);
    pal_gpio_set_level(LED2_GPIO, PAL_GPIO_LEVEL_LOW);

    // Initialise hsys_led instances — each creates its own soft timer internally
    if (!hsys_led_init(&_led1, _led1_on, _led1_off)) {
        LOG_MSG_ERROR(LEDS_LOG_EN, "hsys_led_init failed for LED1");
    }
    if (!hsys_led_init(&_led2, _led2_on, _led2_off)) {
        LOG_MSG_ERROR(LEDS_LOG_EN, "hsys_led_init failed for LED2");
    }

    subscribe(MsgSpiffsReady::ID);

    LOG_MSG_INFO(LEDS_LOG_EN, "init — LEDs off, waiting for SPIFFS_READY");
}

// ── Message handler ────────────────────────────────────────────────────────────

void ModuleLeds::on_msg_received(const hsys_msg_t &msg)
{
    switch (msg.msg_id) {
        case MsgSpiffsReady::ID:
            LOG_MSG_INFO(LEDS_LOG_EN, "SPIFFS ready — starting blink");
            _start_blink();
            break;
        default:
            break;
    }
}

// ── Private ────────────────────────────────────────────────────────────────────

void ModuleLeds::_start_blink()
{
    // 0b01, length=2: step0=OFF(125ms), step1=ON(125ms), repeat forever
    // → 250 ms period, 50% duty cycle
    hsys_led_set_pattern(&_led1, 0b01, 2, 0xFF);
    hsys_led_set_pattern(&_led2, 0b01, 2, 0xFF);

    hsys_led_start(&_led1);
    hsys_led_start(&_led2);

    LOG_MSG_INFO(LEDS_LOG_EN, "LED1 + LED2 blinking at 250 ms");
}
