// app_hw_config.h
//
// Compile-time hardware constants shared across application modules.
// Physical I2C addresses, pin numbers, and other board-level parameters go here.

#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// I2C device addresses (fixed by chip specification)
// ---------------------------------------------------------------------------
#define APP_HW_I2C_ADDR_DS1307   ((uint8_t)0x68)   ///< DS1307 RTC — address is hardwired

// ---------------------------------------------------------------------------
// I2C port assignment
// ---------------------------------------------------------------------------
#define APP_HW_I2C_PORT_RTC      PAL_I2C_PORT_0    ///< I2C port used for the RTC

// ---------------------------------------------------------------------------
// SD card SPI pins  (board 2404)
// ---------------------------------------------------------------------------
#define APP_HW_SD_MOSI           23   ///< GPIO_NUM_23
#define APP_HW_SD_MISO           19   ///< GPIO_NUM_19
#define APP_HW_SD_SCK            18   ///< GPIO_NUM_18
#define APP_HW_SD_CS             15   ///< GPIO_NUM_15

// GPIO pin assignments (board_2602: INPUT1 / PRINT1, INPUT2 / PRINT2)
#define PRINT1_BTN_GPIO         35
#define PRINT2_BTN_GPIO         34

// GPIO pin assignment — OUTPUT2 on board_2602
#define BUZ_GPIO  26

// GPIO pin assignment (board_2602: INPUT5 / DEFAULT_BUTTON_GPIO_PIN)
#define DEFAULT_BTN_GPIO  36

// GPIO pin assignments (matches board_2602 aliases / pal_mac_gpio pin table)
#define LED1_GPIO  5
#define LED2_GPIO  4

#define NOZZLE1_GPIO 32   ///< GPIO_NUM_32, INPUT3 on board_2602
#define NOZZLE2_GPIO 33   ///< GPIO_NUM_33, INPUT4 on board
