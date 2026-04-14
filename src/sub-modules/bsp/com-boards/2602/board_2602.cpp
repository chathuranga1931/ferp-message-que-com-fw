
#include <stdio.h>

#include "pal_preprocessor.h"
#include "pal_logger.h"
#include "pal_gpio.h"
#include "pal_uart.h"
#include "pal_power.h"
#include "pal_time.h"

#include "hsys_queue.h"

#include "board.h"
#include "board_2602_wrap.h"

#define __TAG__  "BRD_2602"

static hsys_queue_handle_t button_event_queue;

bool gpio_get_input1() { return pal_gpio_read(INPUT1); }
bool gpio_get_input2() { return pal_gpio_read(INPUT2); }
bool gpio_get_input3() { return pal_gpio_read(INPUT3); }
bool gpio_get_input4() { return pal_gpio_read(INPUT4); }
bool gpio_get_input5() { return pal_gpio_read(INPUT5); }
bool gpio_get_switch() { return pal_gpio_read(SWITCH); }

void gpio_set_output1(const bool level) { pal_gpio_write(OUTPUT1, level); }
void gpio_set_output2(const bool level) { pal_gpio_write(OUTPUT2, level); }
void gpio_set_output3(const bool level) { pal_gpio_write(OUTPUT3, level); }
void gpio_set_output4(const bool level) { pal_gpio_write(OUTPUT4, level); }
void gpio_set_output5(const bool level) { pal_gpio_write(OUTPUT5, level); }
void gpio_set_output6(const bool level) { pal_gpio_write(OUTPUT6, level); }
void gpio_set_led1(const bool level) { pal_gpio_write(LED1, level); }
void gpio_set_led2(const bool level) { pal_gpio_write(LED2, level); }
void gpio_set_en4g(const bool level) { pal_gpio_write(EN_4G, level); }
void gpio_set_reset_distap(const bool level) { pal_gpio_write(RESET_DISTAP, level); }

void gpio_set_io0_distap(const bool level) 
{ 
    pal_gpio_write(IO0_DISTAP, level); 
}

void gpio_set_mode_output_io0_distap()
{
    pal_gpio_set_direction(IO0_DISTAP, PAL_GPIO_MODE_OUTPUT);
    pal_gpio_write(IO0_DISTAP, false);
}

void gpio_reset_io0_distap()
{
    pal_gpio_set_direction(IO0_DISTAP, PAL_GPIO_MODE_INPUT);
}

esp_err_t board_init()
{
    logger.init();

    esp_err_t ret = ESP_OK;

    // Set Inputs using PAL GPIO
    pal_gpio_config(INPUT1,   PAL_GPIO_CFG_INPUT());
    pal_gpio_config(INPUT2,   PAL_GPIO_CFG_INPUT());
    pal_gpio_config(INPUT3,   PAL_GPIO_CFG_INPUT());
    pal_gpio_config(INPUT4,   PAL_GPIO_CFG_INPUT());
    pal_gpio_config(INPUT5,   PAL_GPIO_CFG_INPUT());
    pal_gpio_config(VIN_LOW,  PAL_GPIO_CFG_INPUT()); 
    pal_gpio_config(VIN_LOW, PAL_GPIO_CFG_INPUT()); 

    // Set Outputs using PAL GPIO
    // NOTE: gpio_reset_pin() is called inside pal_gpio_config() which detaches
    // any analog/RTC peripheral (e.g. DAC on GPIO 25/26) before configuring the
    // pin as a digital output — same behaviour as Arduino's perimanSetPinBus().
    pal_gpio_config(OUTPUT1,    PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(OUTPUT1,    false);
    pal_gpio_config(OUTPUT2,    PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(OUTPUT2,    false);
    pal_gpio_config(OUTPUT3,    PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(OUTPUT3,    false);
    pal_gpio_config(OUTPUT4,    PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(OUTPUT4,    false);
    pal_gpio_config(OUTPUT5,    PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(OUTPUT5,    false);
    pal_gpio_config(OUTPUT6,    PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(OUTPUT6,    false);
    pal_gpio_config(ESP_LED1,   PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(ESP_LED1,       false);
    pal_gpio_config(ESP_LED2,   PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(ESP_LED2,       false);

    pal_gpio_config(EN_4G,      PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(EN_4G,      false);
    pal_gpio_config(SPI_CS_SD,  PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(SPI_CS_SD,  false);
    pal_gpio_config(RESET_DISTAP,PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(RESET_DISTAP,false);
    pal_gpio_config(IO0_DISTAP,  PAL_GPIO_CFG_OUTPUT()); pal_gpio_write(IO0_DISTAP,true);

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

void board_buz_on()
{
    pal_gpio_write(OUTPUT2, true);
}

void board_buz_off()
{
    pal_gpio_write(OUTPUT2, false);
}

void board_led1_on()
{
    pal_gpio_write(LED1, true);
}

void board_led1_off()
{
    pal_gpio_write(LED1, false);
}

void board_led2_on()
{
    pal_gpio_write(LED2, true);
}

void board_led2_off()
{
    pal_gpio_write(LED2, false);
}

