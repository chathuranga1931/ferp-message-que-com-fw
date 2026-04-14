#ifndef _BOARD_2303_H_
#define _BOARD_2303_H_

#include "esp_err.h"


// INPUTS
#define INPUT1          GPIO_NUM_34
#define INPUT2          GPIO_NUM_35
#define INPUT3          GPIO_NUM_32
#define INPUT4          GPIO_NUM_33
#define INPUT5          GPIO_NUM_36
#define SWITCH          INPUT5
#define VIN_LOW         GPIO_NUM_39

// OUTPUTS
#define RESET_ESP32     GPIO_NUM_5
#define OUTPUT1         GPIO_NUM_25
#define OUTPUT2         GPIO_NUM_26
#define OUTPUT3         GPIO_NUM_27
#define OUTPUT4         GPIO_NUM_14
#define OUTPUT5         GPIO_NUM_12
#define OUTPUT6         GPIO_NUM_13
#define EN_4G           GPIO_NUM_2
#define RESET_ESP07     GPIO_NUM_0
#define IO0_ESP07       GPIO_NUM_4
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

#ifdef __cplusplus
extern "C"
{
#endif


/**
 * Initialise sample
 *
 * @param 
 *
 * @return
 *          - ESP_OK if successful
 *          - (else) Invalid
 */
esp_err_t init_board();

bool gpio_get_input1();
bool gpio_get_input2();
bool gpio_get_input3();
bool gpio_get_input4();
bool gpio_get_input5();

void gpio_set_output1(const bool level);
void gpio_set_output2(const bool level);
void gpio_set_output3(const bool level);
void gpio_set_output4(const bool level);
void gpio_set_output5(const bool level);
void gpio_set_output6(const bool level);
void gpio_set_en4g(const bool level);
void gpio_set_reset_esp07(const bool level);

void gpio_set_io0_esp07(const bool level);
void gpio_set_mode_output_io0_esp07();
void gpio_reset_io0_esp07();

void gpio_set_chiprst();
#ifdef __cplusplus
}
#endif

#endif // _BOARD_2303_H_