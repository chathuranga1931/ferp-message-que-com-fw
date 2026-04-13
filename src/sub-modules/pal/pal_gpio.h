/**
 * @file pal_gpio.h
 * @brief Platform Abstraction Layer - GPIO Interface
 * 
 * This file defines a platform-independent interface for GPIO operations.
 * Implementations are provided per platform (ESP-IDF, Arduino, Linux, etc.)
 */

#ifndef PAL_GPIO_H
#define PAL_GPIO_H

#include "pal_types.h"

/*===========================================================================*/
/*                            GPIO PIN NUMBERS                               */
/*===========================================================================*/

/**
 * @brief GPIO pin number definitions (ESP32 compatible)
 * Platform-independent pin number mapping
 */
#define PAL_GPIO_NUM_0      0
#define PAL_GPIO_NUM_1      1
#define PAL_GPIO_NUM_2      2
#define PAL_GPIO_NUM_3      3
#define PAL_GPIO_NUM_4      4
#define PAL_GPIO_NUM_5      5
#define PAL_GPIO_NUM_6      6
#define PAL_GPIO_NUM_7      7
#define PAL_GPIO_NUM_8      8
#define PAL_GPIO_NUM_9      9
#define PAL_GPIO_NUM_10     10
#define PAL_GPIO_NUM_11     11
#define PAL_GPIO_NUM_12     12
#define PAL_GPIO_NUM_13     13
#define PAL_GPIO_NUM_14     14
#define PAL_GPIO_NUM_15     15
#define PAL_GPIO_NUM_16     16
#define PAL_GPIO_NUM_17     17
#define PAL_GPIO_NUM_18     18
#define PAL_GPIO_NUM_19     19
#define PAL_GPIO_NUM_20     20
#define PAL_GPIO_NUM_21     21
#define PAL_GPIO_NUM_22     22
#define PAL_GPIO_NUM_23     23
#define PAL_GPIO_NUM_24     24
#define PAL_GPIO_NUM_25     25
#define PAL_GPIO_NUM_26     26
#define PAL_GPIO_NUM_27     27
#define PAL_GPIO_NUM_28     28
#define PAL_GPIO_NUM_29     29
#define PAL_GPIO_NUM_30     30
#define PAL_GPIO_NUM_31     31
#define PAL_GPIO_NUM_32     32
#define PAL_GPIO_NUM_33     33
#define PAL_GPIO_NUM_34     34
#define PAL_GPIO_NUM_35     35
#define PAL_GPIO_NUM_36     36
#define PAL_GPIO_NUM_37     37
#define PAL_GPIO_NUM_38     38
#define PAL_GPIO_NUM_39     39
#define PAL_GPIO_NUM_MAX    40
// #define PAL_GPIO_NUM_NC     -1      // Not Connected / Invalid GPIO

/*===========================================================================*/
/*                            GPIO TYPES                                     */
/*===========================================================================*/

/**
 * @brief GPIO direction
 */
typedef enum {
    PAL_GPIO_DIR_INPUT  = 0,        // Pin is an input
    PAL_GPIO_DIR_OUTPUT = 1,        // Pin is an output
} pal_gpio_dir_t;

/**
 * @brief GPIO output drive strength
 *
 * Controls how much current the pad can source/sink.
 * Use PAL_GPIO_DRIVE_DEFAULT to leave the hardware default (typically 20 mA).
 * Use PAL_GPIO_DRIVE_STRONG (40 mA) when driving transistor bases or loads
 * that need a solid low/high level with no floating behaviour.
 */
typedef enum {
    PAL_GPIO_DRIVE_DEFAULT = -1,    // Do not change drive strength (hardware default)
    PAL_GPIO_DRIVE_WEAK    =  0,    //  5 mA  — GPIO_DRIVE_CAP_0
    PAL_GPIO_DRIVE_MEDIUM  =  1,    // 10 mA  — GPIO_DRIVE_CAP_1
    PAL_GPIO_DRIVE_NORMAL  =  2,    // 20 mA  — GPIO_DRIVE_CAP_2 (reset default)
    PAL_GPIO_DRIVE_STRONG  =  3,    // 40 mA  — GPIO_DRIVE_CAP_3
} pal_gpio_drive_t;

/**
 * @brief GPIO pull resistor configuration
 */
typedef enum {
    PAL_GPIO_PULL_NONE  = 0,        // No pull resistor
    PAL_GPIO_PULL_UP    = 1,        // Pull-up resistor
    PAL_GPIO_PULL_DOWN  = 2,        // Pull-down resistor
} pal_gpio_pull_t;

/**
 * @brief GPIO interrupt trigger type
 */
typedef enum {
    PAL_GPIO_INTR_DISABLE    = 0,   // No interrupt
    PAL_GPIO_INTR_POSEDGE,          // Rising edge
    PAL_GPIO_INTR_NEGEDGE,          // Falling edge
    PAL_GPIO_INTR_ANYEDGE,          // Both edges
    PAL_GPIO_INTR_LOW_LEVEL,        // Low level
    PAL_GPIO_INTR_HIGH_LEVEL,       // High level
} pal_gpio_intr_type_t;

/**
 * @brief GPIO interrupt callback function type
 *
 * @param gpio_num  GPIO pin number that triggered the interrupt
 * @param arg       User argument passed during configuration
 */
typedef void (*pal_gpio_isr_t)(pal_gpio_num_t gpio_num, void* arg);

/**
 * @brief GPIO logic levels
 */
typedef enum {
    PAL_GPIO_LEVEL_LOW  = 0,        // Logic low  (0 V)
    PAL_GPIO_LEVEL_HIGH = 1,        // Logic high (3.3 V / 5 V)
} pal_gpio_level_t;

/**
 * @brief Complete GPIO pin configuration — passed to pal_gpio_config().
 *
 * Covers both electrical and interrupt properties in a single struct so
 * one call to pal_gpio_config() fully describes a pin.
 *
 *   intr_type = PAL_GPIO_INTR_DISABLE  →  no interrupt (isr_callback ignored)
 *   intr_type = anything else          →  interrupt registered and enabled
 */
typedef struct {
    /* --- electrical --- */
    pal_gpio_dir_t       dir;           // Direction: input or output
    pal_gpio_pull_t      pull;          // Pull resistor
    bool                 open_drain;    // true = open-drain, false = push-pull
    pal_gpio_drive_t     drive;         // Output drive strength (ignored for inputs)

    /* --- interrupt (ignored when intr_type == PAL_GPIO_INTR_DISABLE) --- */
    pal_gpio_intr_type_t intr_type;     // Trigger type
    pal_gpio_isr_t       isr_callback;  // Callback (NULL = none)
    void                *isr_arg;       // Opaque argument forwarded to callback
} pal_gpio_config_t;

/* ---------- Convenience initialisers ------------------------------------ */

/** Push-pull output, no pull, no interrupt, strong drive (40 mA) */
#define PAL_GPIO_CFG_OUTPUT() \
    ((pal_gpio_config_t){ .dir = PAL_GPIO_DIR_OUTPUT, .pull = PAL_GPIO_PULL_NONE, \
                          .open_drain = false, .drive = PAL_GPIO_DRIVE_STRONG, \
                          .intr_type = PAL_GPIO_INTR_DISABLE, \
                          .isr_callback = NULL, .isr_arg = NULL })

/** Open-drain output, no pull, no interrupt, strong drive */
#define PAL_GPIO_CFG_OUTPUT_OD() \
    ((pal_gpio_config_t){ .dir = PAL_GPIO_DIR_OUTPUT, .pull = PAL_GPIO_PULL_NONE, \
                          .open_drain = true, .drive = PAL_GPIO_DRIVE_STRONG, \
                          .intr_type = PAL_GPIO_INTR_DISABLE, \
                          .isr_callback = NULL, .isr_arg = NULL })

/** Floating input, no pull, no interrupt */
#define PAL_GPIO_CFG_INPUT() \
    ((pal_gpio_config_t){ .dir = PAL_GPIO_DIR_INPUT, .pull = PAL_GPIO_PULL_NONE, \
                          .open_drain = false, .drive = PAL_GPIO_DRIVE_DEFAULT, \
                          .intr_type = PAL_GPIO_INTR_DISABLE, \
                          .isr_callback = NULL, .isr_arg = NULL })

/** Input with pull-up, no interrupt */
#define PAL_GPIO_CFG_INPUT_PULLUP() \
    ((pal_gpio_config_t){ .dir = PAL_GPIO_DIR_INPUT, .pull = PAL_GPIO_PULL_UP, \
                          .open_drain = false, .drive = PAL_GPIO_DRIVE_DEFAULT, \
                          .intr_type = PAL_GPIO_INTR_DISABLE, \
                          .isr_callback = NULL, .isr_arg = NULL })

/** Input with pull-down, no interrupt */
#define PAL_GPIO_CFG_INPUT_PULLDOWN() \
    ((pal_gpio_config_t){ .dir = PAL_GPIO_DIR_INPUT, .pull = PAL_GPIO_PULL_DOWN, \
                          .open_drain = false, .drive = PAL_GPIO_DRIVE_DEFAULT, \
                          .intr_type = PAL_GPIO_INTR_DISABLE, \
                          .isr_callback = NULL, .isr_arg = NULL })

/** Input — interrupt on rising edge, pull-up */
#define PAL_GPIO_CFG_INPUT_INTR_RISE(cb, arg) \
    ((pal_gpio_config_t){ .dir = PAL_GPIO_DIR_INPUT, .pull = PAL_GPIO_PULL_UP, \
                          .open_drain = false, .drive = PAL_GPIO_DRIVE_DEFAULT, \
                          .intr_type = PAL_GPIO_INTR_POSEDGE, \
                          .isr_callback = (cb), .isr_arg = (arg) })

/** Input — interrupt on falling edge, pull-up */
#define PAL_GPIO_CFG_INPUT_INTR_FALL(cb, arg) \
    ((pal_gpio_config_t){ .dir = PAL_GPIO_DIR_INPUT, .pull = PAL_GPIO_PULL_UP, \
                          .open_drain = false, .drive = PAL_GPIO_DRIVE_DEFAULT, \
                          .intr_type = PAL_GPIO_INTR_NEGEDGE, \
                          .isr_callback = (cb), .isr_arg = (arg) })

/** Input — interrupt on any edge, pull-up */
#define PAL_GPIO_CFG_INPUT_INTR_ANY(cb, arg) \
    ((pal_gpio_config_t){ .dir = PAL_GPIO_DIR_INPUT, .pull = PAL_GPIO_PULL_UP, \
                          .open_drain = false, .drive = PAL_GPIO_DRIVE_DEFAULT, \
                          .intr_type = PAL_GPIO_INTR_ANYEDGE, \
                          .isr_callback = (cb), .isr_arg = (arg) })

/*===========================================================================*/
/*                       GPIO BASIC OPERATIONS                               */
/*===========================================================================*/

/**
 * @brief Initialize GPIO subsystem
 * 
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_gpio_init(void);

/**
 * @brief Configure a GPIO pin.
 *
 * Applies the full pin configuration (direction, pull, open-drain).
 * On ESP32 this also calls gpio_reset_pin() first to detach any analog /
 * RTC peripheral that may be holding the pad (same behaviour as Arduino's
 * peripheral manager / perimanSetPinBus).
 *
 * @param gpio   GPIO pin number
 * @param config Pin configuration
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_gpio_config(pal_gpio_num_t gpio, pal_gpio_config_t config);

/**
 * @brief Set GPIO output level
 * 
 * @param gpio GPIO pin number
 * @param level Logic level to set
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_gpio_set_level(pal_gpio_num_t gpio, pal_gpio_level_t level);

/**
 * @brief Read GPIO input level
 * 
 * @param gpio GPIO pin number
 * @param level Pointer to store read level
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_gpio_get_level(pal_gpio_num_t gpio, pal_gpio_level_t* level);

/**
 * @brief Toggle GPIO output level
 * 
 * @param gpio GPIO pin number
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_gpio_toggle(pal_gpio_num_t gpio);

/*===========================================================================*/
/*                      GPIO INTERRUPT OPERATIONS                            */
/*===========================================================================*/

/**
 * @brief Configure GPIO interrupt
 * 
 * @param gpio GPIO pin number
 * @param type Interrupt type
 * @param callback Callback function to call on interrupt
 * @param arg User argument to pass to callback
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_gpio_set_interrupt(pal_gpio_num_t gpio, pal_gpio_intr_type_t type, 
                                pal_gpio_isr_t callback, void* arg);

/**
 * @brief Enable GPIO interrupt
 * 
 * @param gpio GPIO pin number
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_gpio_enable_interrupt(pal_gpio_num_t gpio);

/**
 * @brief Disable GPIO interrupt
 * 
 * @param gpio GPIO pin number
 * @return PAL_OK on success, error code otherwise
 */
int32_t pal_gpio_disable_interrupt(pal_gpio_num_t gpio);

/*===========================================================================*/
/*                       CONVENIENCE WRAPPER FUNCTIONS                       */
/*===========================================================================*/

/**
 * @brief Set GPIO direction — convenience wrapper for pal_gpio_config().
 *
 * Accepts the legacy PAL_GPIO_MODE_* tokens so existing call sites continue
 * to compile without modification.
 *
 * Usage:
 *   pal_gpio_set_direction(PIN, PAL_GPIO_MODE_OUTPUT);
 *   pal_gpio_set_direction(PIN, PAL_GPIO_MODE_INPUT_PULLUP);
 */
typedef enum {
    PAL_GPIO_MODE_INPUT          = 0,
    PAL_GPIO_MODE_OUTPUT         = 1,
    PAL_GPIO_MODE_INPUT_PULLUP   = 2,
    PAL_GPIO_MODE_INPUT_PULLDOWN = 3,
} pal_gpio_mode_compat_t;

static inline int32_t pal_gpio_set_direction(pal_gpio_num_t gpio, pal_gpio_mode_compat_t mode) {
    pal_gpio_config_t cfg;
    switch (mode) {
        case PAL_GPIO_MODE_OUTPUT:
            cfg = PAL_GPIO_CFG_OUTPUT();
            break;
        case PAL_GPIO_MODE_INPUT_PULLUP:
            cfg = PAL_GPIO_CFG_INPUT_PULLUP();
            break;
        case PAL_GPIO_MODE_INPUT_PULLDOWN:
            cfg = PAL_GPIO_CFG_INPUT_PULLDOWN();
            break;
        case PAL_GPIO_MODE_INPUT:
        default:
            cfg = PAL_GPIO_CFG_INPUT();
            break;
    }
    return pal_gpio_config(gpio, cfg);
}

/**
 * @brief Write to GPIO (convenience wrapper for pal_gpio_set_level)
 */
static inline int32_t pal_gpio_write(pal_gpio_num_t gpio, bool level) {
    return pal_gpio_set_level(gpio, level ? PAL_GPIO_LEVEL_HIGH : PAL_GPIO_LEVEL_LOW);
}

/**
 * @brief Read from GPIO (convenience wrapper for pal_gpio_get_level)
 */
static inline bool pal_gpio_read(pal_gpio_num_t gpio) {
    pal_gpio_level_t level;
    pal_gpio_get_level(gpio, &level);
    return (level == PAL_GPIO_LEVEL_HIGH);
}

#endif // PAL_GPIO_H
