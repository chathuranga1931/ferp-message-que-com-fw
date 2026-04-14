/**
 * @file .h
 * @author 
 * @Date 2020-01-11
 * @Description	
 * @version 1.0
 * @note Initial draft
 **/

#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__
/*===================================================.======================================================*/
/*                                              DEFINITIONS                                                 */
/*==========================================================================================================*/
#ifndef TRUE
#define TRUE		(1)
#endif

#ifndef FALSE
#define FALSE		(0)
#endif

#ifdef TEST_DEVICE
#define CONFIG_CLOUD_PUSH_RETRY_DELAY_AFTER_LAST_FAIL		(15000)	//in milli seconds
#define LOG_ENABLED		(TRUE)
#define LOGGER_UART		(Serial)
#else
#define LOG_ENABLED		(TRUE)
#define LOGGER_UART		(Serial)
#define CONFIG_CLOUD_PUSH_RETRY_DELAY_AFTER_LAST_FAIL		(300000)	//in milli seconds
#endif

#ifdef FERP_COM
#define NOZZEL_EVENT_QUE_SIZE		(5)
#define PRINTING_EVENT_QUE_SIZE		(2)
#endif
// #ifdef FERP_PRINTER
#define PRINT_EVENT_QUE_SIZE		(10)
// #endif

#define NO_NOZZELS      (2)


#endif //__USER_CONFIG_H__