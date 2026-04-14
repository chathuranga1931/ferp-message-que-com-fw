#ifndef __LED_CUE_H__
#define __LED_CUE_H__

#include "error.h"
#include "device_config.h"

void led_red_blink_delay(uint16_t delay);
void led_green1_blink_delay(uint16_t delay);
void led_green2_blink_delay(uint16_t delay);
void led_process(void);
void led_init(void);

#endif //__LED_CUE_H__