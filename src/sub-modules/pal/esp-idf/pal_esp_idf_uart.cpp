/*===================================================.======================================================*/
/*                                             FILE DESCRIPTION												*/
/*==========================================================================================================*/
/**
* @file		: pal_esp_idf_uart.cpp
* @author	: AI Assistant
* @date		: 23 Feb 2026
*
* @brief	: ESP-IDF implementation of PAL UART interface
*
* @note		: Wraps ESP-IDF UART driver
*/

/*===================================================.======================================================*/
/*												 REFERENCES													*/
/*==========================================================================================================*/
#include "pal/pal_uart.h"
#include <driver/uart.h>
#include <driver/gpio.h>
#include <string.h>

/*===================================================.======================================================*/
/*                                           PRIVATE FUNCTIONS                                              */
/*==========================================================================================================*/

/**
 * @brief Convert PAL UART port to ESP-IDF UART port
 */
static uart_port_t convert_uart_port(pal_uart_port_t port) {
    return (uart_port_t)port;
}

/**
 * @brief Convert PAL data bits to ESP-IDF data bits
 */
static uart_word_length_t convert_data_bits(pal_uart_data_bits_t data_bits) {
    switch (data_bits) {
        case PAL_UART_DATA_5_BITS: return UART_DATA_5_BITS;
        case PAL_UART_DATA_6_BITS: return UART_DATA_6_BITS;
        case PAL_UART_DATA_7_BITS: return UART_DATA_7_BITS;
        case PAL_UART_DATA_8_BITS: return UART_DATA_8_BITS;
        default: return UART_DATA_8_BITS;
    }
}

/**
 * @brief Convert PAL parity to ESP-IDF parity
 */
static uart_parity_t convert_parity(pal_uart_parity_t parity) {
    switch (parity) {
        case PAL_UART_PARITY_DISABLE: return UART_PARITY_DISABLE;
        case PAL_UART_PARITY_EVEN: return UART_PARITY_EVEN;
        case PAL_UART_PARITY_ODD: return UART_PARITY_ODD;
        default: return UART_PARITY_DISABLE;
    }
}

/**
 * @brief Convert PAL stop bits to ESP-IDF stop bits
 */
static uart_stop_bits_t convert_stop_bits(pal_uart_stop_bits_t stop_bits) {
    switch (stop_bits) {
        case PAL_UART_STOP_BITS_1: return UART_STOP_BITS_1;
        case PAL_UART_STOP_BITS_1_5: return UART_STOP_BITS_1_5;
        case PAL_UART_STOP_BITS_2: return UART_STOP_BITS_2;
        default: return UART_STOP_BITS_1;
    }
}

/**
 * @brief Convert PAL flow control to ESP-IDF flow control
 */
static uart_hw_flowcontrol_t convert_flow_ctrl(pal_uart_hw_flowcontrol_t flow_ctrl) {
    switch (flow_ctrl) {
        case PAL_UART_HW_FLOWCTRL_DISABLE: return UART_HW_FLOWCTRL_DISABLE;
        case PAL_UART_HW_FLOWCTRL_RTS: return UART_HW_FLOWCTRL_RTS;
        case PAL_UART_HW_FLOWCTRL_CTS: return UART_HW_FLOWCTRL_CTS;
        case PAL_UART_HW_FLOWCTRL_CTS_RTS: return UART_HW_FLOWCTRL_CTS_RTS;
        default: return UART_HW_FLOWCTRL_DISABLE;
    }
}

/*===================================================.======================================================*/
/*										PUBLIC FUNCTIONS IMPLEMENTATION										*/
/*==========================================================================================================*/

int32_t pal_uart_init(const pal_uart_config_t* config) {
    if (config == NULL) {
        return PAL_ERROR;
    }

    uart_port_t port = convert_uart_port(config->port);

    // Configure UART parameters
    uart_config_t uart_config = {
        .baud_rate = (int)config->baud_rate,
        .data_bits = convert_data_bits(config->data_bits),
        .parity = convert_parity(config->parity),
        .stop_bits = convert_stop_bits(config->stop_bits),
        .flow_ctrl = convert_flow_ctrl(config->flow_ctrl),
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install UART driver
    esp_err_t ret = uart_driver_install(port, config->rx_buffer_size, config->tx_buffer_size, 0, NULL, 0);
    if (ret != ESP_OK) {
        return PAL_ERROR;
    }

    // Configure UART parameters
    ret = uart_param_config(port, &uart_config);
    if (ret != ESP_OK) {
        uart_driver_delete(port);
        return PAL_ERROR;
    }

    // Set UART pins
    ret = uart_set_pin(port, 
                      config->tx_pin >= 0 ? (gpio_num_t)config->tx_pin : UART_PIN_NO_CHANGE,
                      config->rx_pin >= 0 ? (gpio_num_t)config->rx_pin : UART_PIN_NO_CHANGE,
                      config->rts_pin >= 0 ? (gpio_num_t)config->rts_pin : UART_PIN_NO_CHANGE,
                      config->cts_pin >= 0 ? (gpio_num_t)config->cts_pin : UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        uart_driver_delete(port);
        return PAL_ERROR;
    }

    return PAL_OK;
}

int32_t pal_uart_deinit(pal_uart_port_t port) {
    uart_port_t uart_port = convert_uart_port(port);
    esp_err_t ret = uart_driver_delete(uart_port);
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_uart_write(pal_uart_port_t port, const uint8_t* data, size_t length) {
    if (data == NULL) {
        return PAL_ERROR;
    }

    uart_port_t uart_port = convert_uart_port(port);
    int written = uart_write_bytes(uart_port, (const char*)data, length);
    return written;
}

int32_t pal_uart_write_string(pal_uart_port_t port, const char* str) {
    if (str == NULL) {
        return PAL_ERROR;
    }

    return pal_uart_write(port, (const uint8_t*)str, strlen(str));
}

int32_t pal_uart_read(pal_uart_port_t port, uint8_t* data, size_t length, uint32_t timeout_ms) {
    if (data == NULL) {
        return PAL_ERROR;
    }

    uart_port_t uart_port = convert_uart_port(port);
    int read_bytes = uart_read_bytes(uart_port, data, length, timeout_ms / portTICK_PERIOD_MS);
    return read_bytes;
}

int32_t pal_uart_available(pal_uart_port_t port) {
    uart_port_t uart_port = convert_uart_port(port);
    size_t available = 0;
    esp_err_t ret = uart_get_buffered_data_len(uart_port, &available);
    return (ret == ESP_OK) ? (int32_t)available : PAL_ERROR;
}

int32_t pal_uart_flush_tx(pal_uart_port_t port) {
    uart_port_t uart_port = convert_uart_port(port);
    esp_err_t ret = uart_wait_tx_done(uart_port, portMAX_DELAY);
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}

int32_t pal_uart_flush_rx(pal_uart_port_t port) {
    uart_port_t uart_port = convert_uart_port(port);
    esp_err_t ret = uart_flush_input(uart_port);
    return (ret == ESP_OK) ? PAL_OK : PAL_ERROR;
}
