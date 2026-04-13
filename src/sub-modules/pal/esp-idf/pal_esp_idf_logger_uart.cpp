/*===================================================.======================================================*/
/*                                             FILE DESCRIPTION												*/
/*==========================================================================================================*/
/**
* @file		: logger_uart.h
* @author	: ChathurangaD
* @date		: 29 May 2019
*
* @brief	: Abstract the hardware layer functionalities for UART write. 
*
* @note		: Initial draft.
*/

/*===================================================.======================================================*/
/*												 REFERENCES													*/
/*==========================================================================================================*/
#include "pal_logger_uart.h"
#include "pal/pal_uart.h"
#include "pal/pal_gpio.h"
#include <string.h>

/*===================================================.======================================================*/
/*                                           PRIVATE VARIABLES                                              */
/*==========================================================================================================*/
#define UART_LOG_PORT       PAL_UART_PORT_0
#define UART_TX_PIN         PAL_GPIO_NUM_1
#define UART_RX_PIN         PAL_GPIO_NUM_3

static bool uart_initialized = false;

/*===================================================.======================================================*/
/*										PUBLIC FUNCTIONS IMPLEMENTATION										*/
/*==========================================================================================================*/

/**
* @name		: logger_uart
* @brief	: Constructor of the logger_uart. 
*
* This initialize the UART Port with 115200 baudrate, unless it is 
* initialized before. 
*
* @param	: void
* @return	: void
**/

logger_uart::logger_uart(){
#if defined(LOG_ENABLED) && (LOG_ENABLED == TRUE)
	if (!uart_initialized) {
		// Configure PAL UART
		pal_uart_config_t uart_config = {
			.port = UART_LOG_PORT,
			.baud_rate = LOG_UART_BAUDRATE,
			.data_bits = PAL_UART_DATA_8_BITS,
			.parity = PAL_UART_PARITY_DISABLE,
			.stop_bits = PAL_UART_STOP_BITS_1,
			.flow_ctrl = PAL_UART_HW_FLOWCTRL_DISABLE,
			.tx_pin = UART_TX_PIN,
			.rx_pin = UART_RX_PIN,
			.rts_pin = -1,
			.cts_pin = -1,
			.rx_buffer_size = 256,
			.tx_buffer_size = 0
		};
		
		// Initialize UART through PAL interface
		if (pal_uart_init(&uart_config) == PAL_OK) {
			uart_initialized = true;
		}
	}
#endif
}

/**
* @name		: logger_uart
* @brief	: Destructor of the logger_uart.
*
* @param	: void
* @return	: void
**/
logger_uart::~logger_uart(){
	// UART will be cleaned up when needed
}

/**
* @name		: print_char_buff
* @brief	: Print a null-terminated character buffer to UART
*
* @param	: str - Null-terminated string to write to UART port
* @return	: void
**/
void logger_uart::print_char_buff(const char * str){
#if LOG_ENABLED
	if (str != NULL && uart_initialized) {
		pal_uart_write_string(UART_LOG_PORT, str);
	}
#endif
}

/**
* @name		: print_str
* @brief	: Print a null-terminated string to UART
*
* @param	: str - Null-terminated string to write to UART port
* @return	: void
**/
void logger_uart::print_str(const char * str){
#if LOG_ENABLED
	if (str != NULL && uart_initialized) {
		pal_uart_write_string(UART_LOG_PORT, str);
	}
#endif
}
