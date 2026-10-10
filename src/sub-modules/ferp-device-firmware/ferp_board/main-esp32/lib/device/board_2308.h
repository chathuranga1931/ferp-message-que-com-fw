#ifndef __BOARD_2308_H__
#define __BOARD_2308_H__

// Board 2308 (2308-modified) — ferp-com v1, ESP07 DisplayTap.  Included through board.h.
//
// No LEDs on this board.  GPIO5 is the ESP32 reset line (RESET_ESP32) and
// GPIO4 is the ESP07 IO0 line — never use them as outputs outside the DT
// flashing sequence.

#define BOARD_TYPE 2303   // board id the production test stores in the RTC EEPROM (as the earlier firmware)

// INPUTS
#define INPUT1          GPIO_NUM_34
#define INPUT2          GPIO_NUM_35
#define INPUT3          GPIO_NUM_32
#define INPUT4          GPIO_NUM_33
#define INPUT5          GPIO_NUM_36
#define SWITCH          INPUT5
#define VIN_LOW         GPIO_NUM_39
// OUTPUTS
#define RESET_ESP32     GPIO_NUM_5      // ESP32 reset — not driven by the firmware
#define OUTPUT1         GPIO_NUM_25
#define OUTPUT2         GPIO_NUM_26
#define OUTPUT3         GPIO_NUM_27
#define OUTPUT4         GPIO_NUM_14
#define OUTPUT5         GPIO_NUM_12
#define OUTPUT6         GPIO_NUM_13
#define EN_4G           GPIO_NUM_2
#define RESET_DISTAP    GPIO_NUM_0      // ESP07 reset
#define IO0_DISTAP      GPIO_NUM_4      // ESP07 IO0 — input, output only while flashing the ESP07
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
#define UART2_BAUDRATE 115200

#endif // __BOARD_2308_H__
