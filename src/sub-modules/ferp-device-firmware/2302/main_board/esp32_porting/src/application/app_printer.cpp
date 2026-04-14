
/*===================================================.===================================================*/
/*                                             FILE DESCRIPTION                                          */
/*=======================================================================================================*/
/**
* @file		: logger.h
* @author	: 
* @date		: 
*
* @brief	: 
*
* @note		: 
*/

/*===================================================.===================================================*/
/*                                               REFERENCES                                              */
/*=======================================================================================================*/

#include <Arduino.h>
// #include <FS.h>
#include <SPIFFS.h>
#include "ESPAsyncWebServer.h"
// #include <HTTPClient.h>
// #include <AsyncTCP.h>
#include <WebSerial.h>
#include <AsyncElegantOTA.h>
#include <ESPmDNS.h>
#include <Wire.h>

#include "device_config.h"
#include "user_config.h"
#include "logger.h"
// #include "app.h"
// #include "error.h"

// #include "utility/que.h"
// #include "nozzel_event.h"
// #include "board.h"

// #include "sd_api.h"
#include "sd_logger.h"
// #include "display_tap.h"

#include "wifi_manager.h"
#include "date_time_manger.h"
// #include "retransmit_manager.h"
#include "internet_connectivity.h"
// #include "display1_decoder.h"
// #include "led_cue.h"
// #include "ferp_client.h"
// #include "printer_client.h"
#include "print_event.h"
#include "que.h"
#include "printer.h"

/*===================================================.===================================================*/
/*                                              DEFINITIONS                                              */
/*=======================================================================================================*/

/*===================================================.===================================================*/
/*                                                 MACROS                                                */
/*=======================================================================================================*/


/*===================================================.===================================================*/
/*                                                EXTERNS                                                */
/*=======================================================================================================*/
extern device_configs_t g_device_config;

/*===================================================.===================================================*/
/*                                            GLOBAL VARIABLES                                           */
/*=======================================================================================================*/
extern SemaphoreHandle_t  mutex_print_event_que;
print_event_t pe;
print_event_t print_event_buffer[PRINT_EVENT_QUE_SIZE];
que_t print_event_que = {0};

void app_printer_init(){

	ret_t ret = ret_Success;
	logger.log("FERP_PRINTER");

	ret = printer_server_init(&g_device_config);
	error_handler(ret);

    print_event_que.size_of_type = sizeof(print_event_t);
	print_event_que.copy = print_event_copy;
	print_event_que.buffer_size = PRINT_EVENT_QUE_SIZE;
	print_event_que.buffer = print_event_buffer;

	ret = printer_init(&g_device_config, &print_event_que);
	error_handler(ret);

	printer_start_server(&print_event_que);

	mutex_print_event_que = xSemaphoreCreateMutex();
}

void app_printer_run(){

    if( xSemaphoreTake( mutex_print_event_que, ( TickType_t ) 10 ) == pdTRUE ){
        if(que_getsize(&print_event_que) > 0){
            if(que_pop(&print_event_que, &pe) != -1){
                xSemaphoreGive(mutex_print_event_que);
                logger.log("Pop one print event " + pe.time_stamp);
                printer_print(&pe);
            }
            else{
                xSemaphoreGive(mutex_print_event_que); // release mutex
            }
        }
        else{
            xSemaphoreGive(mutex_print_event_que); // release mutex
        }
    }

    static unsigned long ts_0;
    if(millis() - ts_0 > 3000){
        ts_0 = millis();
        logger.log("Printer keep alive...");
        printer_keep_connected();
    }

            static unsigned long ts_5mins;
    if(millis() - ts_5mins > 300000){
        ts_5mins = millis();

    }
}