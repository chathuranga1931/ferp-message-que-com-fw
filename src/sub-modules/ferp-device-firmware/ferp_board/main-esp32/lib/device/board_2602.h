#ifndef __BOARD_2602_H__
#define __BOARD_2602_H__

// Board 2602 — ferp-com v3, ESP32 DisplayTap.  Included through board.h.
// Shared / strapping pins and the per-build checklist: src/product/ferp-com-v3-main/BOARD_CHECKLIST.md

#define BOARD_TYPE 2602

// INPUTS
#define INPUT1          GPIO_NUM_34
#define INPUT2          GPIO_NUM_35
#define INPUT3          GPIO_NUM_32
#define INPUT4          GPIO_NUM_33
#define INPUT5          GPIO_NUM_36
#define SWITCH          INPUT5
#define VIN_LOW         GPIO_NUM_39
// OUTPUTS
#define OUTPUT1         GPIO_NUM_25
#define OUTPUT2         GPIO_NUM_26
#define OUTPUT3         GPIO_NUM_27
#define OUTPUT4         GPIO_NUM_14
#define OUTPUT5         GPIO_NUM_12
#define OUTPUT6         GPIO_NUM_13
#define EN_4G           GPIO_NUM_2
#define RESET_DISTAP    GPIO_NUM_0
#define IO0_DISTAP      GPIO_NUM_4
#define ESP_LED1        GPIO_NUM_5
#define ESP_LED2        IO0_DISTAP
// The DT can be hardware-reset safely here: IO0_DISTAP is a main output kept high (LED2 is not
// driven by the app), so a reset always boots the DT app, never its ROM download mode.
#define BOARD_DT_RESET_SAFE 1
// UART_NUM_2
#define UART2_TX        GPIO_NUM_17
#define UART2_RX        GPIO_NUM_16
// SPI
#define SPI_MOSI        GPIO_NUM_23
#define SPI_MISO        GPIO_NUM_19
#define SPI_SCLK        GPIO_NUM_18
#define SPI_CS_SD       GPIO_NUM_15
// I2C
#define I2C_SCL         GPIO_NUM_22
#define I2C_SDA         GPIO_NUM_21

//UART2
#define UART2_BAUDRATE 115200 //230400 //115200

#ifdef __cplusplus
extern "C"
{
#endif

// LEDs — 2602 only (LED1 on GPIO5, LED2 shares GPIO4 with the DT IO0 line)
void gpio_set_led1(const bool level);
void gpio_set_led2(const bool level);

#ifdef __cplusplus
}
#endif

#endif // __BOARD_2602_H__
