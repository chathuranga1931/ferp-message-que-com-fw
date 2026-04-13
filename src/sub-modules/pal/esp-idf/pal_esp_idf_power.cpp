/*===================================================.======================================================*/
/*                                             FILE DESCRIPTION												*/
/*==========================================================================================================*/
/**
* @file		: pal_esp_idf_power.cpp
* @author	: AI Assistant
* @date		: 23 Feb 2026
*
* @brief	: ESP-IDF implementation of PAL power management interface
*
* @note		: Wraps ESP-IDF system and power management APIs
*/

/*===================================================.======================================================*/
/*												 REFERENCES													*/
/*==========================================================================================================*/
#include "pal/pal_power.h"
#include <esp_system.h>
#include <esp_sleep.h>
#include <rom/rtc.h>

/*===================================================.======================================================*/
/*                                           PRIVATE FUNCTIONS                                              */
/*==========================================================================================================*/

/**
 * @brief Convert ESP-IDF reset reason to PAL reset reason
 */
static pal_reset_reason_t convert_reset_reason(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON:
            return PAL_RESET_REASON_POWER_ON;
        case ESP_RST_EXT:
            return PAL_RESET_REASON_EXTERNAL;
        case ESP_RST_SW:
            return PAL_RESET_REASON_SOFTWARE;
        case ESP_RST_PANIC:
            return PAL_RESET_REASON_PANIC;
        case ESP_RST_INT_WDT:
            return PAL_RESET_REASON_INT_WDT;
        case ESP_RST_TASK_WDT:
            return PAL_RESET_REASON_TASK_WDT;
        case ESP_RST_WDT:
            return PAL_RESET_REASON_WATCHDOG;
        case ESP_RST_DEEPSLEEP:
            return PAL_RESET_REASON_DEEP_SLEEP;
        case ESP_RST_BROWNOUT:
            return PAL_RESET_REASON_BROWNOUT;
        case ESP_RST_SDIO:
            return PAL_RESET_REASON_SDIO;
        default:
            return PAL_RESET_REASON_UNKNOWN;
    }
}

/**
 * @brief Convert ESP-IDF wakeup cause to PAL wakeup source
 */
static uint32_t convert_wakeup_cause(esp_sleep_wakeup_cause_t cause) {
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            return PAL_WAKEUP_SOURCE_TIMER;
        case ESP_SLEEP_WAKEUP_EXT0:
            return PAL_WAKEUP_SOURCE_EXT0;
        case ESP_SLEEP_WAKEUP_EXT1:
            return PAL_WAKEUP_SOURCE_EXT1;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            return PAL_WAKEUP_SOURCE_TOUCHPAD;
        case ESP_SLEEP_WAKEUP_ULP:
            return PAL_WAKEUP_SOURCE_ULP;
        case ESP_SLEEP_WAKEUP_GPIO:
            return PAL_WAKEUP_SOURCE_GPIO;
        default:
            return 0;
    }
}

/*===================================================.======================================================*/
/*										PUBLIC FUNCTIONS IMPLEMENTATION										*/
/*==========================================================================================================*/

void pal_power_reset(void) {
    esp_restart();
    // This function never returns
}

pal_reset_reason_t pal_power_get_reset_reason(void) {
    esp_reset_reason_t reason = esp_reset_reason();
    return convert_reset_reason(reason);
}

int32_t pal_power_deep_sleep(uint64_t sleep_time_us) {
    if (sleep_time_us > 0) {
        esp_sleep_enable_timer_wakeup(sleep_time_us);
    }
    
    esp_deep_sleep_start();
    // This function typically does not return
    return PAL_OK;
}

int32_t pal_power_light_sleep(uint64_t sleep_time_us) {
    if (sleep_time_us > 0) {
        esp_sleep_enable_timer_wakeup(sleep_time_us);
    }
    
    esp_err_t ret = esp_light_sleep_start();
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_power_enable_timer_wakeup(uint64_t time_us) {
    esp_err_t ret = esp_sleep_enable_timer_wakeup(time_us);
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_power_enable_ext_wakeup(int32_t gpio_num, int32_t level) {
    if (gpio_num < 0) {
        return PAL_ERROR;
    }
    
    esp_err_t ret = esp_sleep_enable_ext0_wakeup((gpio_num_t)gpio_num, level);
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

uint32_t pal_power_get_wakeup_source(void) {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    return convert_wakeup_cause(cause);
}

int32_t pal_power_get_voltage_mv(void) {
    // ESP32 doesn't have built-in voltage measurement without ADC
    // This would require external circuitry or ADC reading
    // Return error as not implemented
    return PAL_ERROR;
}

int32_t pal_power_set_domain(uint32_t domain, bool enable) {
    // ESP-IDF handles power domains automatically
    // This could be extended for specific peripheral power control
    // For now, return not implemented
    return PAL_ERROR;
}
