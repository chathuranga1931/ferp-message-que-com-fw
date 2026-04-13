/**
 * @file pal_esp_idf_gpio.cpp
 * @brief Platform Abstraction Layer - ESP-IDF GPIO Implementation
 * 
 * This file implements the GPIO interface for ESP-IDF platform using
 * the GPIO driver.
 */

#include "pal_gpio.h"

#include <stdlib.h>
#include "driver/gpio.h"
#include "esp_attr.h"

#include "pal_logger.h"

/*===========================================================================*/
/*                            DEFINITIONS                                    */
/*===========================================================================*/

#define __TAG__ "PAL_GPIO"

/*===========================================================================*/
/*                          HELPER FUNCTIONS                                 */
/*===========================================================================*/

/**
 * @brief Convert PAL GPIO number to ESP-IDF GPIO number
 */
static gpio_num_t convert_gpio_num(int32_t pal_gpio_num) {
    // Direct mapping for ESP32 - PAL GPIO numbers map 1:1 with ESP-IDF GPIO_NUM_x
    return (gpio_num_t)pal_gpio_num;
}

/**
 * @brief Convert PAL interrupt type to ESP-IDF interrupt type
 */
static gpio_int_type_t convert_interrupt_type(pal_gpio_intr_type_t type) {
    switch(type) {
        case PAL_GPIO_INTR_POSEDGE:    return GPIO_INTR_POSEDGE;
        case PAL_GPIO_INTR_NEGEDGE:    return GPIO_INTR_NEGEDGE;
        case PAL_GPIO_INTR_ANYEDGE:    return GPIO_INTR_ANYEDGE;
        case PAL_GPIO_INTR_LOW_LEVEL:  return GPIO_INTR_LOW_LEVEL;
        case PAL_GPIO_INTR_HIGH_LEVEL: return GPIO_INTR_HIGH_LEVEL;
        default:                       return GPIO_INTR_DISABLE;
    }
}

/**
 * @brief Convert PAL drive strength to ESP-IDF gpio_drive_cap_t
 *
 * ESP-IDF drive options (from hal/gpio_types.h):
 *   GPIO_DRIVE_CAP_0  ~5  mA  (weakest)
 *   GPIO_DRIVE_CAP_1  ~10 mA
 *   GPIO_DRIVE_CAP_2  ~20 mA  (hardware reset default = GPIO_DRIVE_CAP_DEFAULT)
 *   GPIO_DRIVE_CAP_3  ~40 mA  (strongest)
 *
 * PAL values map 1:1 to these (0-3), so the cast is safe.
 * PAL_GPIO_DRIVE_DEFAULT (-1) means "do not call gpio_set_drive_capability".
 */
static gpio_drive_cap_t convert_drive_strength(pal_gpio_drive_t drive) {
    switch (drive) {
        case PAL_GPIO_DRIVE_WEAK:   return GPIO_DRIVE_CAP_0;   //  ~5 mA
        case PAL_GPIO_DRIVE_MEDIUM: return GPIO_DRIVE_CAP_1;   // ~10 mA
        case PAL_GPIO_DRIVE_NORMAL: return GPIO_DRIVE_CAP_2;   // ~20 mA (HW default)
        case PAL_GPIO_DRIVE_STRONG: return GPIO_DRIVE_CAP_3;   // ~40 mA
        default:                    return GPIO_DRIVE_CAP_DEFAULT;
    }
}

/*===========================================================================*/
/*                       ISR CONTEXT (forward declaration)                  */
/*===========================================================================*/

/**
 * @brief GPIO ISR context — holds the callback and its arguments.
 * Declared here so pal_gpio_config() can allocate it before the interrupt
 * section further down in the file.
 */
typedef struct {
    pal_gpio_isr_t callback;
    pal_gpio_num_t gpio_num;
    void*          user_arg;
} gpio_isr_context_t;

/**
 * @brief ESP-IDF ISR handler wrapper — calls the PAL callback.
 */
static void IRAM_ATTR gpio_isr_handler_wrapper(void* arg) {
    gpio_isr_context_t* ctx = (gpio_isr_context_t*)arg;
    if (ctx && ctx->callback) {
        ctx->callback(ctx->gpio_num, ctx->user_arg);
    }
}

/*===========================================================================*/
/*                       GPIO BASIC FUNCTIONS                                */
/*===========================================================================*/

int32_t pal_gpio_init(void) {
    // GPIO driver is initialized automatically by ESP-IDF
    // No explicit initialization needed
    return PAL_OK;
}

int32_t pal_gpio_config(pal_gpio_num_t gpio_num, pal_gpio_config_t config) {
    if(gpio_num < 0) {
        LOG_MSG_ERROR(LOG_EN, "Invalid GPIO number");
        return PAL_ERROR_INVALID;
    }

    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);

    // Detach any analog / RTC peripheral that may be holding the pad.
    // This mirrors what Arduino's peripheral manager (perimanSetPinBus) does
    // automatically when pinMode() is called — without this, DAC-capable pins
    // (GPIO 25/26) remain in analog mode and their digital drive is impaired.
    gpio_reset_pin(esp_gpio);

    // --- electrical configuration ---
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask  = (1ULL << esp_gpio);
    io_conf.intr_type     = GPIO_INTR_DISABLE;   // set after electrical config

    if (config.dir == PAL_GPIO_DIR_OUTPUT) {
        io_conf.mode         = config.open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT;
        io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    } else {
        io_conf.mode         = GPIO_MODE_INPUT;
        io_conf.pull_up_en   = (config.pull == PAL_GPIO_PULL_UP)   ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = (config.pull == PAL_GPIO_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    }

    esp_err_t ret = gpio_config(&io_conf);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(LOG_EN, "Failed to configure GPIO %d", gpio_num);
        return PAL_ERROR_INIT;
    }

    // --- drive strength (outputs only) ---
    if (config.dir == PAL_GPIO_DIR_OUTPUT && config.drive != PAL_GPIO_DRIVE_DEFAULT) {
        ret = gpio_set_drive_capability(esp_gpio, convert_drive_strength(config.drive));
        if (ret != ESP_OK) {
            LOG_MSG_ERROR(LOG_EN, "Failed to set drive strength for GPIO %d", gpio_num);
            // non-fatal — continue
        }
    }
    if (config.intr_type != PAL_GPIO_INTR_DISABLE && config.isr_callback != NULL) {
        // Install ISR service on first use
        static bool isr_service_installed = false;
        if (!isr_service_installed) {
            ret = gpio_install_isr_service(0);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                LOG_MSG_ERROR(LOG_EN, "Failed to install ISR service for GPIO %d", gpio_num);
                return PAL_ERROR_INIT;
            }
            isr_service_installed = true;
        }

        // Allocate ISR context (freed on pal_gpio_detach_interrupt)
        gpio_isr_context_t* ctx = (gpio_isr_context_t*)malloc(sizeof(gpio_isr_context_t));
        if (!ctx) { return PAL_ERROR_NO_MEMORY; }
        ctx->callback = config.isr_callback;
        ctx->gpio_num = gpio_num;
        ctx->user_arg = config.isr_arg;

        ret = gpio_set_intr_type(esp_gpio, convert_interrupt_type(config.intr_type));
        if (ret != ESP_OK) { free(ctx); return PAL_ERROR; }

        ret = gpio_isr_handler_add(esp_gpio, gpio_isr_handler_wrapper, ctx);
        if (ret != ESP_OK) { free(ctx); return PAL_ERROR; }

        ret = gpio_intr_enable(esp_gpio);
        if (ret != ESP_OK) { gpio_isr_handler_remove(esp_gpio); free(ctx); return PAL_ERROR; }
    }

    return PAL_OK;
}

int32_t pal_gpio_deinit(pal_gpio_num_t gpio_num) {
    if(gpio_num < 0) {
        return PAL_ERROR_INVALID;
    }
    
    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);
    esp_err_t ret = gpio_reset_pin(esp_gpio);
    if(ret != ESP_OK) {
        return PAL_ERROR;
    }
    
    return PAL_OK;
}

int32_t pal_gpio_set_level(pal_gpio_num_t gpio_num, pal_gpio_level_t level) {
    if(gpio_num < 0) {
        return PAL_ERROR_INVALID;
    }
    
    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);
    esp_err_t ret = gpio_set_level(esp_gpio, (level == PAL_GPIO_LEVEL_HIGH) ? 1 : 0);
    if(ret != ESP_OK) {
        return PAL_ERROR;
    }
    
    return PAL_OK;
}

int32_t pal_gpio_get_level(pal_gpio_num_t gpio_num, pal_gpio_level_t* level) {
    if(gpio_num < 0 || level == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);
    int gpio_level = gpio_get_level(esp_gpio);
    *level = (gpio_level == 1) ? PAL_GPIO_LEVEL_HIGH : PAL_GPIO_LEVEL_LOW;
    
    return PAL_OK;
}

int32_t pal_gpio_toggle(pal_gpio_num_t gpio_num) {
    if(gpio_num < 0) {
        return PAL_ERROR_INVALID;
    }
    
    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);
    int current_level = gpio_get_level(esp_gpio);
    esp_err_t ret = gpio_set_level(esp_gpio, !current_level);
    if(ret != ESP_OK) {
        return PAL_ERROR;
    }
    
    return PAL_OK;
}

/*===========================================================================*/
/*                       GPIO INTERRUPT FUNCTIONS                            */
/*===========================================================================*/

int32_t pal_gpio_enable_interrupt(pal_gpio_num_t gpio_num, pal_gpio_intr_type_t type) {
    if(gpio_num < 0) {
        return PAL_ERROR_INVALID;
    }
    
    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);
    gpio_int_type_t esp_int_type = convert_interrupt_type(type);
    
    esp_err_t ret = gpio_set_intr_type(esp_gpio, esp_int_type);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(LOG_EN, "Failed to set interrupt type for GPIO %d", gpio_num);
        return PAL_ERROR;
    }
    
    ret = gpio_intr_enable(esp_gpio);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(LOG_EN, "Failed to enable interrupt for GPIO %d", gpio_num);
        return PAL_ERROR;
    }
    
    return PAL_OK;
}

int32_t pal_gpio_disable_interrupt(pal_gpio_num_t gpio_num) {
    if(gpio_num < 0) {
        return PAL_ERROR_INVALID;
    }
    
    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);
    esp_err_t ret = gpio_intr_disable(esp_gpio);
    if(ret != ESP_OK) {
        return PAL_ERROR;
    }
    
    return PAL_OK;
}

int32_t pal_gpio_set_interrupt(pal_gpio_num_t gpio_num, pal_gpio_intr_type_t type, 
                                pal_gpio_isr_t callback, void* arg) {
    if(gpio_num < 0 || callback == NULL) {
        return PAL_ERROR_INVALID;
    }
    
    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);
    
    // Install GPIO ISR service if not already installed
    static bool isr_service_installed = false;
    if(!isr_service_installed) {
        esp_err_t ret = gpio_install_isr_service(0);
        if(ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            LOG_MSG_ERROR(LOG_EN, "Failed to install ISR service");
            return PAL_ERROR_INIT;
        }
        isr_service_installed = true;
    }
    
    // Allocate context (Note: In production, use a context pool to avoid memory leaks)
    gpio_isr_context_t* ctx = (gpio_isr_context_t*)malloc(sizeof(gpio_isr_context_t));
    if(!ctx) {
        return PAL_ERROR_NO_MEMORY;
    }
    
    ctx->callback = callback;
    ctx->gpio_num = gpio_num;
    ctx->user_arg = arg;
    
    // Set interrupt type
    gpio_int_type_t esp_int_type = convert_interrupt_type(type);
    esp_err_t ret = gpio_set_intr_type(esp_gpio, esp_int_type);
    if(ret != ESP_OK) {
        free(ctx);
        return PAL_ERROR;
    }
    
    // Add ISR handler
    ret = gpio_isr_handler_add(esp_gpio, gpio_isr_handler_wrapper, ctx);
    if(ret != ESP_OK) {
        LOG_MSG_ERROR(LOG_EN, "Failed to add ISR handler for GPIO %d", gpio_num);
        free(ctx);
        return PAL_ERROR;
    }
    
    // Enable interrupt
    ret = gpio_intr_enable(esp_gpio);
    if(ret != ESP_OK) {
        gpio_isr_handler_remove(esp_gpio);
        free(ctx);
        return PAL_ERROR;
    }
    
    return PAL_OK;
}

int32_t pal_gpio_detach_interrupt(pal_gpio_num_t gpio_num) {
    if(gpio_num < 0) {
        return PAL_ERROR_INVALID;
    }
    
    gpio_num_t esp_gpio = convert_gpio_num(gpio_num);
    
    // Disable interrupt
    gpio_intr_disable(esp_gpio);
    
    // Remove ISR handler
    esp_err_t ret = gpio_isr_handler_remove(esp_gpio);
    if(ret != ESP_OK) {
        return PAL_ERROR;
    }
    
    return PAL_OK;
}
