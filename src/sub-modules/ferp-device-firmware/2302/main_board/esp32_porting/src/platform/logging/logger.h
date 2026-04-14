
/*===================================================.======================================================*/
/*                                             FILE DESCRIPTION												*/
/*==========================================================================================================*/
/**
* @file		: logger.h
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
#ifndef _LOGGER__H_
#define _LOGGER__H_


/*===================================================.======================================================*/
/*												 REFERENCES													*/
/*==========================================================================================================*/
#include "hal_logger_uart.h"

/*===================================================.======================================================*/
/*												DEFINITIONS													*/
/*==========================================================================================================*/

/*===================================================.======================================================*/
/*												   MACROS													*/
/*==========================================================================================================*/
#define LOG_FDETAILS(str) \
	logger.log("[" + String(__FILENAME__) + "]" + String(__LINE__) + str )
#define LOG_FUNCTION() \
	logger.log("[" + String(__FILENAME__) + "]" + String(__func__))
#define LOG_MSG(...) sprintf(sprintf_buff, __VA_ARGS__); \
	logger.log(sprintf_buff)

/*===================================================.======================================================*/
/*											   CLASS DEFINITION 											*/
/*==========================================================================================================*/
class logger_class{
	protected:

	public:
		void log_buffer(byte * buffer, uint8_t size);
		void log_buffer(byte * buffer, uint8_t size, char c);
		void log_buffer(const String str, byte * buffer, uint8_t size);
		void log_uint16_buffer(uint16_t * buffer, uint8_t size);
		void log_buffer_as_uint16(byte * buffer, uint8_t size);
		void log(String str);
		void log_(String str);
		void log_header(void);
		void log_footer(void);
		void log(const Printable& x);

	private:
};

/*===================================================.======================================================*/
/*											  GLOBAL VARIABLES 			    								*/
/*==========================================================================================================*/
extern char sprintf_buff[100];
extern logger_class logger;

#endif /* _LOGGER__H_ */
