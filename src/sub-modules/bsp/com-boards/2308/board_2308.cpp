
#include <stdio.h>
#include "board_2308_wrap.h"
#include "board.h"
#include "pal_logger.h"
#include "pal/pal_gpio.h"
#include "pal/pal_uart.h"
#include "pal/pal_power.h"

#define __TAG__  "BRD_2308"

inline bool gpio_get_input1() { return pal_gpio_read(INPUT1); }
inline bool gpio_get_input2() { return pal_gpio_read(INPUT2); }
inline bool gpio_get_input3() { return pal_gpio_read(INPUT3); }
inline bool gpio_get_input4() { return pal_gpio_read(INPUT4); }
inline bool gpio_get_input5() { return pal_gpio_read(INPUT5); }
inline bool gpio_get_switch() { return pal_gpio_read(SWITCH); }

inline void gpio_set_output1(const bool level) { pal_gpio_write(OUTPUT1, level); }
inline void gpio_set_output2(const bool level) { pal_gpio_write(OUTPUT2, level); }
inline void gpio_set_output3(const bool level) { pal_gpio_write(OUTPUT3, level); }
inline void gpio_set_output4(const bool level) { pal_gpio_write(OUTPUT4, level); }
inline void gpio_set_output5(const bool level) { pal_gpio_write(OUTPUT5, level); }
inline void gpio_set_output6(const bool level) { pal_gpio_write(OUTPUT6, level); }
inline void gpio_set_en4g(const bool level) { pal_gpio_write(EN_4G, level); }
void gpio_set_reset_esp07(const bool level) { pal_gpio_write(RESET_ESP07, !level); }

void gpio_set_io0_esp07(const bool level) { 
    pal_gpio_write(IO0_ESP07, level); 
}

void gpio_set_mode_output_io0_esp07()
{
    pal_gpio_set_direction(IO0_ESP07, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(IO0_ESP07, false);
}

void gpio_reset_io0_esp07()
{
    // Reset GPIO configuration
    pal_gpio_set_direction(IO0_ESP07, PAL_GPIO_MODE_INPUT);
}

void gpio_set_chiprst()
{
    pal_gpio_set_direction(RESET_ESP32, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(RESET_ESP32, false);
}


int32_t board_init()
{
    int32_t ret = 0;
    
    // Set Inputs using PAL GPIO
    pal_gpio_set_direction(INPUT1, PAL_GPIO_MODE_INPUT);
    pal_gpio_set_direction(INPUT2, PAL_GPIO_MODE_INPUT);
    pal_gpio_set_direction(INPUT3, PAL_GPIO_MODE_INPUT);
    pal_gpio_set_direction(INPUT4, PAL_GPIO_MODE_INPUT);
    pal_gpio_set_direction(INPUT5, PAL_GPIO_MODE_INPUT);
    pal_gpio_set_direction(VIN_LOW, PAL_GPIO_MODE_INPUT);

    // Set Outputs using PAL GPIO
    pal_gpio_set_direction(OUTPUT1, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(OUTPUT1, false);
    pal_gpio_set_direction(OUTPUT2, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(OUTPUT2, false);
    pal_gpio_set_direction(OUTPUT3, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(OUTPUT3, false);
    pal_gpio_set_direction(OUTPUT4, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(OUTPUT4, false);
    pal_gpio_set_direction(OUTPUT5, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(OUTPUT5, false);
    pal_gpio_set_direction(OUTPUT6, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(OUTPUT6, false);
    pal_gpio_set_direction(EN_4G, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(EN_4G, false);
    pal_gpio_set_direction(SPI_CS_SD, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(SPI_CS_SD, false);
    pal_gpio_set_direction(RESET_ESP07, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(RESET_ESP07, false);
    pal_gpio_set_direction(IO0_ESP07, PAL_GPIO_MODE_INPUT);

    // Initialize UART2 using PAL interface
    pal_uart_config_t uart2_config = {
        .port = PAL_UART_PORT_2,
        .baud_rate = 115200,
        .data_bits = PAL_UART_DATA_8_BITS,
        .parity = PAL_UART_PARITY_DISABLE,
        .stop_bits = PAL_UART_STOP_BITS_1,
        .flow_ctrl = PAL_UART_HW_FLOWCTRL_DISABLE,
        .tx_pin = UART2_TX,
        .rx_pin = UART2_RX,
        .rts_pin = -1,
        .cts_pin = -1,
        .rx_buffer_size = 512,
        .tx_buffer_size = 512
    };
    pal_uart_init(&uart2_config);
    
    return ret;
}

void board_restart(){
    // Perform system reset using PAL power interface
    pal_power_reset();
}

void board_buz_on(){
    LOG_MSG_DEBUG(LOG_EN, "BUZZER ON");
    pal_gpio_write(OUTPUT2, true);
}

void board_buz_off(){
    LOG_MSG_DEBUG(LOG_EN, "BUZZER OFF");
    pal_gpio_write(OUTPUT2, false);
}
