/**
 * @file pal_mac_gpio.cpp
 * @brief macOS PAL implementation for GPIO.
 *
 * Output path:  pal_gpio_set_level(pin, level) → mac_driver_send_gpio()  → TCP → Python UI
 * Input path:   pal_gpio_sim_inject_input(pin, level) → fires stored ISR callback
 *               (called by mac_driver.cpp read loop on SIM_BTN messages)
 *
 * TODO: full implementation — currently a skeleton to allow the build to link.
 */

#include "pal_gpio.h"
#include "driver/mac_driver.h"

#include <string.h>
#include <stdint.h>

/* ── Internal tables ─────────────────────────────────────────────────────── */

#define PAL_GPIO_MAX  51    /* GPIO_NUM_MAX from driver/gpio.h stub */

static uint8_t          s_output_state[PAL_GPIO_MAX] = {};
static uint8_t          s_input_state[PAL_GPIO_MAX]  = {};

struct isr_entry_t { pal_gpio_isr_t cb; void *arg; };
static isr_entry_t      s_isr_table[PAL_GPIO_MAX]    = {};

/* ── Pin name table (matches board_2602 aliases) ─────────────────────────── */
static const struct { int pin; const char *name; } k_pin_names[] = {
    {  5, "LED1"   },
    {  4, "LED2"   },
    { 26, "BUZZER" },
    { 36, "BTN_DEF"},
    { 34, "BTN_P1" },
    { 35, "BTN_P2" },
    { 32, "NOZZLE1"},
    { 33, "NOZZLE2"},
};
static constexpr int k_pin_names_count = (int)(sizeof(k_pin_names) / sizeof(k_pin_names[0]));

static const char *pin_name(int pin)
{
    for (int i = 0; i < k_pin_names_count; ++i)
        if (k_pin_names[i].pin == pin) return k_pin_names[i].name;
    return "";
}

/* ── pal_gpio.h interface implementation ────────────────────────────────── */

int32_t pal_gpio_init(void) { return PAL_OK; }

int32_t pal_gpio_config(pal_gpio_num_t gpio, pal_gpio_config_t config)
{
    if (gpio < 0 || gpio >= PAL_GPIO_MAX) return PAL_ERROR_INVALID;
    if (config.isr_callback)
    {
        s_isr_table[gpio].cb  = config.isr_callback;
        s_isr_table[gpio].arg = config.isr_arg;
    }
    return PAL_OK;
}

int32_t pal_gpio_set_level(pal_gpio_num_t gpio, pal_gpio_level_t level)
{
    if (gpio < 0 || gpio >= PAL_GPIO_MAX) return PAL_ERROR_INVALID;
    s_output_state[gpio] = (level == PAL_GPIO_LEVEL_HIGH) ? 1 : 0;
    mac_driver_send_gpio(gpio, s_output_state[gpio], pin_name(gpio));
    return PAL_OK;
}

int32_t pal_gpio_get_level(pal_gpio_num_t gpio, pal_gpio_level_t *level)
{
    if (gpio < 0 || gpio >= PAL_GPIO_MAX || !level) return PAL_ERROR_INVALID;
    *level = s_input_state[gpio] ? PAL_GPIO_LEVEL_HIGH : PAL_GPIO_LEVEL_LOW;
    return PAL_OK;
}

int32_t pal_gpio_toggle(pal_gpio_num_t gpio)
{
    if (gpio < 0 || gpio >= PAL_GPIO_MAX) return PAL_ERROR_INVALID;
    pal_gpio_level_t new_level = s_output_state[gpio] ? PAL_GPIO_LEVEL_LOW : PAL_GPIO_LEVEL_HIGH;
    return pal_gpio_set_level(gpio, new_level);
}

int32_t pal_gpio_set_interrupt(pal_gpio_num_t gpio, pal_gpio_intr_type_t type,
                                pal_gpio_isr_t callback, void *arg)
{
    if (gpio < 0 || gpio >= PAL_GPIO_MAX) return PAL_ERROR_INVALID;
    s_isr_table[gpio].cb  = callback;
    s_isr_table[gpio].arg = arg;
    return PAL_OK;
}

int32_t pal_gpio_enable_interrupt(pal_gpio_num_t gpio)  { return PAL_OK; }
int32_t pal_gpio_disable_interrupt(pal_gpio_num_t gpio) { return PAL_OK; }

/* ── Injection — called by mac_driver.cpp on SIM_BTN ─────────────────────── */

extern "C" void pal_gpio_sim_inject_input(int pin, int level)
{
    if (pin < 0 || pin >= PAL_GPIO_MAX) return;
    s_input_state[pin] = (uint8_t)(level ? 1 : 0);
    if (s_isr_table[pin].cb)
        s_isr_table[pin].cb((pal_gpio_num_t)pin, s_isr_table[pin].arg);
}
