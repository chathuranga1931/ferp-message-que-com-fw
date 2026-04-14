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
#include "hal_logger_uart.h"
#include "user_config.h"

/*===================================================.======================================================*/
/*                                           PRIVATE VARIABLES                                              */
/*==========================================================================================================*/


/*===================================================.======================================================*/
/*										PUBLIC FUNCTIONS IMPLEMENTATION										*/
/*==========================================================================================================*/

/**
* @name		: logger_uart
* @brief	: Constructor of the logger_uart. 
*
* This initialize the Serial Port with 115200 baudrate, unless it is 
* initialized before. 
*
* @param	: void
* @return	: void
**/
logger_uart::logger_uart(){
#ifdef LOG_ENABLED == TRUE
	if (!LOGGER_UART){
		LOGGER_UART.begin(115200);
	}
#endif
}

/**
* @name		: logger_uart
* @brief	: Distructor of the logger_uart.
*
* @param	: void
* @return	: void
**/
logger_uart::~logger_uart(){

}

/**
* @name		: print
* @brief	: Constructor of the logger_uart.
*
* This initialize the Serial Port with 115200 baudrate, unless it is
* initialized before.
*
* @param	: String, \0 ended string to write to user configured 
*				UART port. Ex Serial1
* @return	: void
**/
void logger_uart::print(String str){
#if LOG_ENABLED
	LOGGER_UART.print(str);
#endif
}

void logger_uart::print(const Printable& x){
#if LOG_ENABLED
	LOGGER_UART.print(x);
#endif
}
