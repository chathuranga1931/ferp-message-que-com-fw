/*===================================================.======================================================*/
/*                                             FILE DESCRIPTION												*/
/*==========================================================================================================*/
/**
* @file		: pal_power.h
* @author	: AI Assistant
* @date		: 23 Feb 2026
*
* @brief	: Platform Abstraction Layer for power management operations
*
* @note		: Provides platform-independent power management interface
*/

#ifndef _PAL_POWER_H_
#define _PAL_POWER_H_

/*===================================================.======================================================*/
/*												 REFERENCES													*/
/*==========================================================================================================*/
#include "pal_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===================================================.======================================================*/
/*												   TYPES													*/
/*==========================================================================================================*/

/**
 * @brief Reset reasons
 */
typedef enum {
    PAL_RESET_REASON_UNKNOWN = 0,
    PAL_RESET_REASON_POWER_ON,
    PAL_RESET_REASON_EXTERNAL,
    PAL_RESET_REASON_SOFTWARE,
    PAL_RESET_REASON_WATCHDOG,
    PAL_RESET_REASON_DEEP_SLEEP,
    PAL_RESET_REASON_BROWNOUT,
    PAL_RESET_REASON_SDIO,
    PAL_RESET_REASON_PANIC,
    PAL_RESET_REASON_INT_WDT,
    PAL_RESET_REASON_TASK_WDT,
    PAL_RESET_REASON_OTHER_WDT,
    PAL_RESET_REASON_DEEPSLEEP_RESET,
    PAL_RESET_REASON_TG0WDT_SYS_RESET,
    PAL_RESET_REASON_TG1WDT_SYS_RESET,
    PAL_RESET_REASON_RTCWDT_SYS_RESET,
    PAL_RESET_REASON_INTRUSION_RESET,
    PAL_RESET_REASON_MAX
} pal_reset_reason_t;

/**
 * @brief Sleep modes
 */
typedef enum {
    PAL_SLEEP_MODE_LIGHT = 0,
    PAL_SLEEP_MODE_DEEP,
    PAL_SLEEP_MODE_MAX
} pal_sleep_mode_t;

/**
 * @brief Wakeup sources for deep sleep
 */
typedef enum {
    PAL_WAKEUP_SOURCE_TIMER = (1 << 0),
    PAL_WAKEUP_SOURCE_EXT0 = (1 << 1),
    PAL_WAKEUP_SOURCE_EXT1 = (1 << 2),
    PAL_WAKEUP_SOURCE_TOUCHPAD = (1 << 3),
    PAL_WAKEUP_SOURCE_ULP = (1 << 4),
    PAL_WAKEUP_SOURCE_GPIO = (1 << 5)
} pal_wakeup_source_t;

/*===================================================.======================================================*/
/*											   FUNCTION PROTOTYPES											*/
/*==========================================================================================================*/

/**
 * @brief Perform a software reset/restart of the system
 * 
 * This function will reset the entire system. It will not return.
 * All peripherals will be reset to their default state.
 * 
 * @param None
 * @return This function does not return
 */
void pal_power_reset(void);

/**
 * @brief Get the reason for the last reset
 * 
 * @return Reset reason code
 */
pal_reset_reason_t pal_power_get_reset_reason(void);

/**
 * @brief Enter deep sleep mode
 * 
 * @param sleep_time_us Time to sleep in microseconds (0 = indefinite)
 * @return PAL_OK on success, PAL_FAIL on error
 * @note This function may not return if deep sleep is successful
 */
int32_t pal_power_deep_sleep(uint64_t sleep_time_us);

/**
 * @brief Enter light sleep mode
 * 
 * @param sleep_time_us Time to sleep in microseconds
 * @return PAL_OK on success, PAL_FAIL on error
 */
int32_t pal_power_light_sleep(uint64_t sleep_time_us);

/**
 * @brief Enable timer wakeup for deep sleep
 * 
 * @param time_us Time in microseconds after which to wake up
 * @return PAL_OK on success, PAL_FAIL on error
 */
int32_t pal_power_enable_timer_wakeup(uint64_t time_us);

/**
 * @brief Enable external GPIO wakeup for deep sleep
 * 
 * @param gpio_num GPIO pin number
 * @param level Wake up when GPIO is at this level (0 = low, 1 = high)
 * @return PAL_OK on success, PAL_FAIL on error
 */
int32_t pal_power_enable_ext_wakeup(int32_t gpio_num, int32_t level);

/**
 * @brief Get the wakeup source that caused the system to wake from sleep
 * 
 * @return Wakeup source flags (can be combination of pal_wakeup_source_t)
 */
uint32_t pal_power_get_wakeup_source(void);

/**
 * @brief Get current power supply voltage (if available)
 * 
 * @return Voltage in millivolts, or negative error code
 */
int32_t pal_power_get_voltage_mv(void);

/**
 * @brief Enable or disable power to a specific domain/peripheral
 * 
 * @param domain Power domain identifier (platform-specific)
 * @param enable true to enable power, false to disable
 * @return PAL_OK on success, PAL_FAIL on error
 */
int32_t pal_power_set_domain(uint32_t domain, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* _PAL_POWER_H_ */
