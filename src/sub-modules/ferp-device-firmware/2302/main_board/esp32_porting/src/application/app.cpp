
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
#include "board.h"
#include "version.h"

#ifdef FERP_PRINTER
#include "app_printer.h"
#elif FERP_COM
#include "app_com.h"
#endif

/*===================================================.===================================================*/
/*                                              DEFINITIONS                                              */
/*=======================================================================================================*/

/*===================================================.===================================================*/
/*                                                 MACROS                                                */
/*=======================================================================================================*/

/*===================================================.===================================================*/
/*                                            GLOBAL VARIABLES                                           */
/*=======================================================================================================*/
device_configs_t g_device_config;

/*===================================================.===================================================*/
/*                                            PRIVATE VARIABLES                                          */
/*=======================================================================================================*/
static AsyncWebServer 	server(80);             /* Set web server port number to 80 */
static String DNS = "DefaultDNS";

/*===================================================.===================================================*/
/*                                      PRIVATE FUNCTION PROTOTYPES                                      */
/*=======================================================================================================*/
void taskOne(void * parameter );
void message(uint8_t *data, size_t len);
void board_init(void);

/*===================================================.===================================================*/
/*                                       PUBLIC FUNCTIONS DEFINITIONS                                    */
/*=======================================================================================================*/
void app_init(){

    ret_t ret = ret_Success;

    board_init();
	
	logger.log("FW_VERSION = " + String(FW_VERSION));

    g_device_config.async_server = &server;
    ret = device_config_init(&g_device_config);
	error_handler(ret);

    ret = wifi_init(&g_device_config);
	error_handler(ret);

    logger.log("Board MAC Address:  " + String(g_device_config.mac_address));

	String macaddress = String(g_device_config.mac_address);
	macaddress.replace(":", "");
#ifdef FERP_COM
	DNS = "FERP-IoT-Com-" + macaddress;
#elif FERP_PRINTER
	DNS = "FERP-IoT-Printer-" + macaddress;
#endif
	logger.log("DNS: " + DNS);

	if(g_device_config.status.wifi_status == wifi_status_connected){
		if(!MDNS.begin(DNS.c_str())) {
			logger.log("Error starting mDNS");
            write_error("WiFi: mDNS configuration Error");
		}
	}

    g_device_config.status.date_time_status = date_time_sync_failed;
	ret = date_time_init(&g_device_config);

	struct timeval now;
	gettimeofday(&now, NULL);
	g_device_config.power_on_time = now.tv_sec;

	/* Time is updated at this time, so save it at begining.. */
	update_last_working_time();

	ret = device_config_start_server();
	// error_handler(ret);

    WebSerial.begin(&server);
	WebSerial.msgCallback(message);
	AsyncElegantOTA.begin(&server, "FERP", "FERP2873");
	server.begin();

#ifdef FERP_PRINTER
    app_printer_init();
#elif FERP_COM
    app_com_init();
#endif

    xTaskCreate(
		taskOne,          /* Task function. */
        "TaskOne",        /* String with name of task. */
        40*1024,          /* Stack size in bytes. */
        NULL,             /* Parameter passed as input of the task */
        1,                /* Priority of the task. */
        NULL);            /* Task handle. */
}

void app_run(){
    vTaskDelay(1000);
}

/*===================================================.===================================================*/
/*                                      PRIVATE FUNCTIONS DEFINITIONS                                    */
/*=======================================================================================================*/
void board_init(){

    SPIFFS.begin();

#ifdef FERP_COM
	Wire.begin(I2C_SDA, I2C_SCL, 100000);

    pinMode(GPIO_BTN_DEFAULT, INPUT);

	pinMode(GPIO_LED_RED, OUTPUT);
	pinMode(GPIO_LED_GREEN1, OUTPUT);
	pinMode(GPIO_LED_GREEN2, OUTPUT);

	pinMode(GPIO_N_RESET_ESP07, OUTPUT);
	digitalWrite(GPIO_N_RESET_ESP07, HIGH);
#elif FERP_PRINTER
#endif
	
}

void taskOne(void * parameter){

    LOG_FUNCTION();

    if(g_device_config.status.wifi_status == wifi_status_ap_mode){
		g_device_config.wifi.ap_time_stamp = millis();
	}

    while(1){

#ifdef FERP_PRINTER
        app_printer_run();
#elif FERP_COM
        app_com_run();
#endif

        static unsigned long ts_15Sec;
        if(millis() - ts_15Sec > 15000){
            ts_15Sec = millis();

            if(g_device_config.status.wifi_status == wifi_status_connected || 
                g_device_config.status.wifi_status == wifi_status_disconnected){
                if ((WiFi.status() != WL_CONNECTED)) {
                    g_device_config.status.wifi_status = wifi_status_disconnected;
                    g_device_config.status.internet_status = internet_disconnected;
                    logger.log("Reconnecting to WiFi...");
                    WiFi.disconnect();
                    WiFi.reconnect();
                }
                else{
                    g_device_config.status.wifi_status = wifi_status_connected;
                }
            }
            else if(g_device_config.status.wifi_status == wifi_status_ap_mode){
                if(millis() - g_device_config.wifi.ap_time_stamp > 180000){
                    logger.log("Station mode timed out");
                    delay(1000);
                    ESP.restart();
                }
            }
        }

        static unsigned long ts_5mins;
		if(millis() - ts_5mins > 300000){
			ts_5mins = millis();

			/* update time for every five minutes, if the device has not able to get
			 * time from NTP or RTC, can use the last working time stored in SPIFFs */
			update_last_working_time();

		}

		if(g_device_config.status.wifi_status == wifi_status_connected){
				static double rssi_avg;

				rssi_avg = rssi_avg*0.9 + WiFi.RSSI()*0.1;
				/* WiFi Connected, get the RSSI level */
				g_device_config.wifi.rssi = (int8_t)rssi_avg;
				// logger.log("RSSI" + String(g_device_config.wifi.rssi));
		}

		static unsigned long ts_30sec;
		if(millis() - ts_30sec > 30000){
			ts_30sec = millis();

			/* If WiFi connected and Internet is not connected, check internet status 
			 * more fequently */
			if(g_device_config.status.wifi_status == wifi_status_connected){

				if(g_device_config.status.internet_status != internet_connected){
					logger.log("Checking internet...");
					check_internet(&g_device_config);
					if(g_device_config.status.internet_status != internet_connected){
						write_error("Internet: Disconnected");
					}
					else if(g_device_config.status.internet_status == internet_connected){
						write_note("Internet: Connected");
					}
				}
			}
		}

		device_config_process();
		g_device_config.main_thread_counter++;
		vTaskDelay(10);
    }


    while(1){
		logger.log("something is wrong... ");
	}

    vTaskDelete( NULL );

}


void message(uint8_t *data, size_t len) {

	WebSerial.println("Data Received!");
	// String Data = "";
	// for(int i=0; i < len; i++){
	// 	Data += char(data[i]);
	// }
	// WebSerial.println(Data);
	// if (Data == "LED ON"){
	// 	digitalWrite(LED_GPIO, HIGH);
	// }
	// if (Data=="LED OFF"){
	// 	digitalWrite(LED_GPIO, LOW);
	// }
}











