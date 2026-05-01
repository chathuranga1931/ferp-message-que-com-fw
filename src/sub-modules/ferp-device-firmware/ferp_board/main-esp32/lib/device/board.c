#include <stdio.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include "board.h"


inline bool gpio_get_input1() { return gpio_get_level(INPUT1); }
inline bool gpio_get_input2() { return gpio_get_level(INPUT2); }
inline bool gpio_get_input3() { return gpio_get_level(INPUT3); }
inline bool gpio_get_input4() { return gpio_get_level(INPUT4); }
inline bool gpio_get_input5() { return gpio_get_level(INPUT5); }
inline bool gpio_get_switch() { return gpio_get_level(SWITCH); }

inline void gpio_set_output1(const bool level) { gpio_set_level(OUTPUT1, level); }
inline void gpio_set_output2(const bool level) { gpio_set_level(OUTPUT2, level); }
inline void gpio_set_output3(const bool level) { gpio_set_level(OUTPUT3, level); }
inline void gpio_set_output4(const bool level) { gpio_set_level(OUTPUT4, level); }
inline void gpio_set_output5(const bool level) { gpio_set_level(OUTPUT5, level); }
inline void gpio_set_output6(const bool level) { gpio_set_level(OUTPUT6, level); }
inline void gpio_set_led1(const bool level) { gpio_set_level(ESP_LED1, !level); }
inline void gpio_set_led2(const bool level) { gpio_set_level(ESP_LED2, !level); }
inline void gpio_set_en4g(const bool level) { gpio_set_level(EN_4G, level); }
#if BOARD_2303
    inline void gpio_set_reset_distap(const bool level) { gpio_set_level(RESET_DISTAP, !level); }
#else
    inline void gpio_set_reset_distap(const bool level) { gpio_set_level(RESET_DISTAP, level); }
#endif

void gpio_set_io0_distap(const bool level) { 
    // gpio_set_direction(IO0_DISTAP, GPIO_MODE_OUTPUT);
    gpio_set_level(IO0_DISTAP, level); 
}
void gpio_set_mode_output_io0_distap()
{
    gpio_reset_pin(IO0_DISTAP);
    gpio_set_direction(IO0_DISTAP, GPIO_MODE_OUTPUT);
    gpio_set_level(IO0_DISTAP, false);
}
void gpio_reset_io0_distap()
{
    gpio_reset_pin(IO0_DISTAP);
}

esp_err_t board_init()
{
    printf("======================\r\n");
    printf("Starting board init...\r\n");
    printf("======================\r\n");
    
    esp_err_t ret = ESP_OK;
    //Set Inputs
    gpio_reset_pin(INPUT1);
    gpio_set_direction(INPUT1, GPIO_MODE_INPUT);
    gpio_reset_pin(INPUT2);
    gpio_set_direction(INPUT2, GPIO_MODE_INPUT);
    gpio_reset_pin(INPUT3);
    gpio_set_direction(INPUT3, GPIO_MODE_INPUT);
    gpio_reset_pin(INPUT4);
    gpio_set_direction(INPUT4, GPIO_MODE_INPUT);
    gpio_reset_pin(INPUT5);
    gpio_set_direction(INPUT5, GPIO_MODE_INPUT);
    // gpio_reset_pin(SWITCH);
    // gpio_set_direction(SWITCH, GPIO_MODE_INPUT);
    gpio_reset_pin(VIN_LOW);
    gpio_set_direction(VIN_LOW, GPIO_MODE_INPUT);

    //Set Outputs
    gpio_reset_pin(OUTPUT1);
    gpio_set_direction(OUTPUT1, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT1, false);
    gpio_reset_pin(OUTPUT2);
    gpio_set_direction(OUTPUT2, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT2, false);
    gpio_reset_pin(OUTPUT3);
    gpio_set_direction(OUTPUT3, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT3, false);
    gpio_reset_pin(OUTPUT4);
    gpio_set_direction(OUTPUT4, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT4, false);
    gpio_reset_pin(OUTPUT5);
    gpio_set_direction(OUTPUT5, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT5, false);
    gpio_reset_pin(OUTPUT6);
    gpio_set_direction(OUTPUT6, GPIO_MODE_OUTPUT);
    gpio_set_level(OUTPUT6, false);
    gpio_reset_pin(ESP_LED1);
    gpio_set_direction(ESP_LED1, GPIO_MODE_OUTPUT);
    gpio_set_level(ESP_LED1, true); // make LED OFF by default

    gpio_reset_pin(EN_4G);
    gpio_set_direction(EN_4G, GPIO_MODE_OUTPUT);
    gpio_set_level(EN_4G, false);
    gpio_reset_pin(SPI_CS_SD);
    gpio_set_direction(SPI_CS_SD, GPIO_MODE_OUTPUT);
    gpio_set_level(SPI_CS_SD, false);
    gpio_reset_pin(RESET_DISTAP);
    gpio_set_direction(RESET_DISTAP, GPIO_MODE_OUTPUT);
    gpio_set_level(RESET_DISTAP, false);
    gpio_reset_pin(IO0_DISTAP); //use this as ESP_LED2 output
    gpio_set_direction(IO0_DISTAP, GPIO_MODE_OUTPUT);
    gpio_set_level(IO0_DISTAP, true); // make LED OFF by default

    //Init UART2
    if(uart_is_driver_installed(UART_NUM_2))
        uart_driver_delete(UART_NUM_2);
    uart_config_t uart_config = {
        .baud_rate = UART2_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_set_pin(UART_NUM_2, UART2_TX, UART2_RX, GPIO_NUM_NC, GPIO_NUM_NC);
    uart_driver_install(UART_NUM_2, 512, 512, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &uart_config);
    return ret;
}

void board_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}
