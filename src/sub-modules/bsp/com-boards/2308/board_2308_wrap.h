#ifndef _BOARD_2303_H_
#define _BOARD_2303_H_

#include "pal/pal_gpio.h"
#include "pal/pal_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "board_2308.h"

// GPIO compatibility defines
#define GPIO_NUM_NC     PAL_GPIO_NUM_NC

#define DEFAULT_BUTTON_GPIO_PIN    INPUT5
#define PRINT1_BUTTON_GPIO_PIN     INPUT1
#define PRINT2_BUTTON_GPIO_PIN     INPUT2
#define NOZZLE1_GPIO_PIN           INPUT3
#define NOZZLE2_GPIO_PIN           INPUT4


#ifdef __cplusplus
}
#endif

#endif // _BOARD_2303_H_