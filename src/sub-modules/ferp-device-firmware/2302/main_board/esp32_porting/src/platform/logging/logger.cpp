/*===================================================.======================================================*/
/*                                             FILE DESCRIPTION												*/
/*==========================================================================================================*/
/**
* @file		: logger.c
* @author	: ChathurangaD
* @date		: 29 May 2019
*
* @brief	: Act as a logger to any level of implementation. 
*
* This implements the wrapper functions to hardware layer logger api, like file logging or 
* serial port logging. 
* 
*
* @note		: Initial draft,
*			  at this moment, logging is routed all the log messages to uart.
*/

/*===================================================.======================================================*/
/*												 REFERENCES													*/
/*==========================================================================================================*/
#include "logger.h"
#include "sd_logger.h"

/*===================================================.======================================================*/
/*                                           PRIVATE VARIABLES                                              */
/*==========================================================================================================*/
logger_class logger;
char sprintf_buff[100];
logger_uart log_uart;

/*===================================================.======================================================*/
/*										PUBLIC FUNCTIONS IMPLEMENTATION										*/
/*==========================================================================================================*/
/**
* @name		: log
* @brief	: Pass the string to lower level.
*
* @param	: String
* @return	: void
**/
void logger_class::log(String str){
    log_header();
	log_uart.print(str);
	write_debug_log(str);
	log_footer();
}

void logger_class::log_(String str){
	log_uart.print(str);
}

void logger_class::log_header(){
	log_uart.print("[DRC] ");
}

void logger_class::log_footer(){
	log_uart.print("\r\n\0");
}

void logger_class::log(const Printable& x){
    log_header();
	log_uart.print(x);
	log_footer();
}

/**
 * @name		: log_buffer
 * @brief	: Convert byte buffer into HEX formated string and pass
 *			  to lower layer.
 *
 * @param	: byte *, pointer to data buffer that need to be logger.
 * @param	: uint8_t, size of the buffer that need to be logged.
 * @return	: void
 **/
void logger_class::log_buffer(byte * buffer, uint8_t size){

	if(buffer == NULL) return;
    log_header();
	char tmp_buff[7];
	for (uint8_t i = 0; i < size; i++){
		sprintf(tmp_buff, "%02X%c%c", buffer[i], ' ', '\0');
		log_uart.print(tmp_buff);
	}
	log_footer();
}

void logger_class::log_buffer(const String str, uint8_t * buffer, uint8_t size){

	if(buffer == NULL) return;
    log_header();
	log_uart.print(str);
	char tmp_buff[7];
	for (uint8_t i = 0; i < size; i++){
		sprintf(tmp_buff, "%02X%c%c", buffer[i], ' ', '\0');
		log_uart.print(tmp_buff);
	}
	log_footer();
}

void logger_class::log_buffer(byte * buffer, uint8_t size, char c){

	if(buffer == NULL) return;
    log_header();
	char tmp_buff[7];
	for (uint8_t i = 0; i < size; i++){
		sprintf(tmp_buff, "%02X%c%c", buffer[i], c,'\0');
		log_uart.print(tmp_buff);
	}
	log_footer();
}

void logger_class::log_uint16_buffer(uint16_t * buffer, uint8_t size){

	if(buffer == NULL) return;
    log_header();
	char tmp_buff[10];
	for (uint8_t i = 0; i < size; i++){
		sprintf(tmp_buff, "%04X %c", buffer[i], '\0');
		log_uart.print(tmp_buff);
	}
	log_footer();
}

void logger_class::log_buffer_as_uint16(byte * buffer, uint8_t size){

	if(buffer == NULL) return;
    log_header();
	char tmp_buff[7];
	for (uint8_t i = 0; i < size; i+=2){
		sprintf(tmp_buff, "%02X%02X %c", buffer[i+1],buffer[i], '\0');
		log_uart.print(tmp_buff);
	}
	log_footer();
}
