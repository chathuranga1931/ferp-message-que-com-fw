#ifndef _BOARD_2404_H_
#define _BOARD_2404_H_

#include "pal_gpio.h"
#include "pal_types.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define BOARD_TYPE 2602

#include "board_2602.h"

// GPIO compatibility defines
#define GPIO_NUM_NC     PAL_GPIO_NUM_NC

#define LED1    ESP_LED1
#define LED2    ESP_LED2

#define DEFAULT_BUTTON_GPIO_PIN    INPUT5
#define PRINT1_BUTTON_GPIO_PIN     INPUT1
#define PRINT2_BUTTON_GPIO_PIN     INPUT2
#define NOZZLE1_GPIO_PIN           INPUT3
#define NOZZLE2_GPIO_PIN           INPUT4


#ifdef __cplusplus
}
#endif

#endif // _BOARD_2404_H_