/*===================================================.======================================================*/
/*                                             FILE DESCRIPTION												*/
/*==========================================================================================================*/
/**
* @file		: pal_uart.h
* @author	: AI Assistant
* @date		: 23 Feb 2026
*
* @brief	: Platform Abstraction Layer for UART operations
*
* @note		: Provides platform-independent UART interface
*/

#ifndef _PAL_UART_H_
#define _PAL_UART_H_

/*===================================================.======================================================*/
/*												 REFERENCES													*/
/*==========================================================================================================*/
#include "pal_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===================================================.======================================================*/
/*												   TYPES													*/
/*==========================================================================================================*/

/**
 * @brief UART port numbers
 */
typedef enum {
    PAL_UART_PORT_0 = 0,
    PAL_UART_PORT_1 = 1,
    PAL_UART_PORT_2 = 2,
    PAL_UART_PORT_MAX
} pal_uart_port_t;

/**
 * @brief UART data bits
 */
typedef enum {
    PAL_UART_DATA_5_BITS = 5,
    PAL_UART_DATA_6_BITS = 6,
    PAL_UART_DATA_7_BITS = 7,
    PAL_UART_DATA_8_BITS = 8
} pal_uart_data_bits_t;

/**
 * @brief UART parity modes
 */
typedef enum {
    PAL_UART_PARITY_DISABLE = 0,
    PAL_UART_PARITY_EVEN = 1,
    PAL_UART_PARITY_ODD = 2
} pal_uart_parity_t;

/**
 * @brief UART stop bits
 */
typedef enum {
    PAL_UART_STOP_BITS_1 = 1,
    PAL_UART_STOP_BITS_1_5 = 2,
    PAL_UART_STOP_BITS_2 = 3
} pal_uart_stop_bits_t;

/**
 * @brief UART flow control modes
 */
typedef enum {
    PAL_UART_HW_FLOWCTRL_DISABLE = 0,
    PAL_UART_HW_FLOWCTRL_RTS = 1,
    PAL_UART_HW_FLOWCTRL_CTS = 2,
    PAL_UART_HW_FLOWCTRL_CTS_RTS = 3
} pal_uart_hw_flowcontrol_t;

/**
 * @brief UART configuration structure
 */
typedef struct {
    pal_uart_port_t port;
    uint32_t baud_rate;
    pal_uart_data_bits_t data_bits;
    pal_uart_parity_t parity;
    pal_uart_stop_bits_t stop_bits;
    pal_uart_hw_flowcontrol_t flow_ctrl;
    int32_t tx_pin;
    int32_t rx_pin;
    int32_t rts_pin;  // Set to -1 if not used
    int32_t cts_pin;  // Set to -1 if not used
    uint32_t rx_buffer_size;
    uint32_t tx_buffer_size;
} pal_uart_config_t;

/*===================================================.======================================================*/
/*											   FUNCTION PROTOTYPES											*/
/*==========================================================================================================*/

/**
 * @brief Initialize UART with the given configuration
 * 
 * @param config Pointer to UART configuration structure
 * @return PAL_OK on success, PAL_FAIL on error
 */
int32_t pal_uart_init(const pal_uart_config_t* config);

/**
 * @brief Deinitialize UART
 * 
 * @param port UART port number
 * @return PAL_OK on success, PAL_FAIL on error
 */
int32_t pal_uart_deinit(pal_uart_port_t port);

/**
 * @brief Write data to UART
 * 
 * @param port UART port number
 * @param data Pointer to data buffer to write
 * @param length Number of bytes to write
 * @return Number of bytes written, or negative error code
 */
int32_t pal_uart_write(pal_uart_port_t port, const uint8_t* data, size_t length);

/**
 * @brief Write string to UART
 * 
 * @param port UART port number
 * @param str Null-terminated string to write
 * @return Number of bytes written, or negative error code
 */
int32_t pal_uart_write_string(pal_uart_port_t port, const char* str);

/**
 * @brief Read data from UART
 * 
 * @param port UART port number
 * @param data Pointer to buffer to store read data
 * @param length Maximum number of bytes to read
 * @param timeout_ms Timeout in milliseconds
 * @return Number of bytes read, or negative error code
 */
int32_t pal_uart_read(pal_uart_port_t port, uint8_t* data, size_t length, uint32_t timeout_ms);

/**
 * @brief Get number of bytes available in RX buffer
 * 
 * @param port UART port number
 * @return Number of bytes available, or negative error code
 */
int32_t pal_uart_available(pal_uart_port_t port);

/**
 * @brief Flush UART TX buffer (wait for all data to be transmitted)
 * 
 * @param port UART port number
 * @return PAL_OK on success, PAL_FAIL on error
 */
int32_t pal_uart_flush_tx(pal_uart_port_t port);

/**
 * @brief Clear UART RX buffer
 * 
 * @param port UART port number
 * @return PAL_OK on success, PAL_FAIL on error
 */
int32_t pal_uart_flush_rx(pal_uart_port_t port);

#ifdef __cplusplus
}
#endif

#endif /* _PAL_UART_H_ */
