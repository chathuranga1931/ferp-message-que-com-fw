
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "ESPAsyncWebServer.h"
#include <HTTPClient.h>
#include "logger.h"
#include <AsyncTCP.h>
#include <WebSerial.h>
#include <AsyncElegantOTA.h>
#include <ESPmDNS.h>
#include <Wire.h>

#include "device_config.h"
#include "user_config.h"
#include "app.h"
#include "error.h"

#include "utility/que.h"
#include "nozzel_event.h"
// #include "sdkconfig.h"

#ifdef FERP_PRINTER
#include "print_event.h"
// #include <Arduino_FreeRTOS.h>
// #include "semphr.h"

extern SemaphoreHandle_t  mutex_print_event_que;
#endif

device_configs_t g_device_config;

#define RXD2 16
#define TXD2 17

// Set web server port number to 80
AsyncWebServer 	server(80);

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

// handles uploads



void send_data(){
	// String url = "http://example.org";
	// HTTPClient http;
  	// http.begin(url);

	// int httpResponseCode = http.GET();
	// if (httpResponseCode > 0) {
	// 	Serial.print("HTTP ");
	// 	Serial.println(httpResponseCode);
	// 	String payload = http.getString();
	// 	Serial.println();
	// 	Serial.println(payload);
	// }
	// else {
	// 	Serial.print("Error code: ");
	// 	Serial.println(httpResponseCode);
	// 	Serial.println(":-(");
	// }

	String url = "http://192.168.1.3:8080";
	HTTPClient http;
  	http.begin(url);
	http.addHeader("Content-Type", "text/plain"); //Specify content-type header
	int httpResponseCode = http.POST("POSTING from ESP32"); //Send the actual POST request

	// http.addHeader("Content-Type", "application/json"); //Specify content-type header
	// int httpResponseCode = http.POST("{\"api_key\":\"tPmAT5Ab3j7F9\",\"sensor\":\"BME280\",\"value1\":\"24.25\",\"value2\":\"49.54\",\"value3\":\"1005.14\"}"); //Send the actual POST request

	if (httpResponseCode > 0) {
		Serial.print("HTTP ");
		Serial.println(httpResponseCode);
		String payload = http.getString();
		Serial.println();
		Serial.println(payload);
	}
	else {
		Serial.print("Error code: ");
		Serial.println(httpResponseCode);
		Serial.println(":-(");
	}
}


extern ret_t wifi_init(device_configs_t* device_configs);
extern ret_t date_time_init(device_configs_t * device_configs);
extern ret_t device_config_init(device_configs_t * device_configs);
extern ret_t device_config_start_server(void);

extern ret_t get_time(struct tm * timeinfo);
extern ret_t aes_setkey(unsigned char* key);
extern ret_t aes_decrypt(unsigned char * cypertext, unsigned int input_size, char * plaintext, unsigned int output_size);
extern ret_t aes_encrypt(char * plaintext, unsigned int input_size, unsigned char * cypertext, unsigned int output_size);

#ifdef FERP_COM
#define NOZZEL_EVENT_QUE_SIZE		(5)
#define PRINTING_EVENT_QUE_SIZE		(2)
extern void sd_init(device_configs_t * device_configs);

extern void on_data_received(unsigned char b);
extern void display_decoder_init(device_configs_t * device_configs, que_t * n_event_que, que_t * printing_event_que);

extern ret_t ferp_client_init(device_configs_t * device_configs);
extern ret_t ferp_push_data(nozzel_event_t * n_event);

extern ret_t printer_client_init(device_configs_t * device_configs);
extern ret_t printer_push_data(nozzel_event_t * n_event);

nozzel_event_t nozzel_event_buffer[NOZZEL_EVENT_QUE_SIZE];
que_t nozzel_event_que = {0};
nozzel_event_t printing_event_buffer[PRINTING_EVENT_QUE_SIZE];
que_t printing_event_que = {0};

void display_decoder_process();
#endif

#ifdef FERP_PRINTER

#define PRINT_EVENT_QUE_SIZE		(10)

extern ret_t printer_init(device_configs_t * device_configs, que_t * ptint_que);
extern ret_t printer_start_server(que_t * ptint_que);
extern ret_t printer_server_init(device_configs_t * device_configs);
extern ret_t printer_print(print_event_t * pe);
extern void printer_keep_connected();

print_event_t print_event_buffer[PRINT_EVENT_QUE_SIZE];
que_t print_event_que = {0};
#endif

void taskOne( void * parameter );

void error_handler(ret_t ret){

	if(ret == ret_Success){
		return;
	}

	logger.log("Error =" + String(ret));

	while(1){
		delay(500);
	}
}

void app_init(){

#ifdef FERP_COM
	logger.log("FERP-COM");
#else
	logger.log("FERP_PRINTER");
#endif

	ret_t ret = ret_Success;

    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
	SPIFFS.begin();

	/* Used in RTC */
#ifdef FERP_COM
	Wire.begin();
    Wire.setClock(100000);
#endif

    g_device_config.async_server = &server;
    ret = device_config_init(&g_device_config);
	error_handler(ret);

#ifdef FERP_PRINTER
	ret = printer_server_init(&g_device_config);
	error_handler(ret);
#endif

    ret = wifi_init(&g_device_config);
	error_handler(ret);

	logger.log("ESP Board MAC Address:  " + String(g_device_config.mac_address));

	String macaddress = String(g_device_config.mac_address);
	macaddress.replace(":", "");
#ifdef FERP_COM
	String DNS = "FERP-IoT-Com" + macaddress;
#endif
#ifdef FERP_PRINTER
	String DNS = "FERP-IoT-Printer" + macaddress;
#endif
	logger.log("DNS: " + DNS);

	if(!MDNS.begin(DNS.c_str())) {
		logger.log("Error starting mDNS");
	}

	struct tm before;
	get_time(&before);
	ret = date_time_init(&g_device_config);
	error_handler(ret);
	struct tm after;
	get_time(&after);

	ret = device_config_start_server();
	error_handler(ret);

	unsigned char aes_key[16] = {0};
	for(int i=0; i<6; i++){
		aes_key[i] = g_device_config.mac_address[i];
	}
	for(int i=0; i<6; i++){
		aes_key[i+6] = g_device_config.mac_address[5-i];
	}
	for(int i=0; i<4; i++){
		aes_key[i+12] = g_device_config.mac_address[i];
	}

	aes_setkey(aes_key);

#ifdef TEST_AES	
	char plain_text [] = "This is a plain text...\0";
	unsigned char cyper_text[strlen(plain_text) + 16] = {0};
	char decrypt_text[sizeof(plain_text) + 16] = {0};
	aes_encrypt(plain_text, strlen(plain_text), cyper_text, strlen(plain_text)+16);
	aes_decrypt(cyper_text, strlen(plain_text) + 16, decrypt_text, strlen(plain_text)+16);

	logger.log_buffer("AES: plain_text = ", (byte *)plain_text, strlen(plain_text));
	logger.log_buffer("AES: cyper_text = ", (byte *)cyper_text, strlen(plain_text)+16);
	logger.log_buffer("AES: decrypt_text = ", (byte *)decrypt_text, strlen(plain_text)+16);
#endif

#ifdef FERP_COM
	sd_init(&g_device_config);
#endif

	WebSerial.begin(&server);
	WebSerial.msgCallback(message);
	AsyncElegantOTA.begin(&server, "FERP", "FERP2873");

	server.begin();

#ifdef FERP_COM

	nozzel_event_que.size_of_type = sizeof(nozzel_event_t);
	nozzel_event_que.copy = nozzel_event_copy;
	nozzel_event_que.buffer_size = NOZZEL_EVENT_QUE_SIZE;
	nozzel_event_que.buffer = nozzel_event_buffer;

	printing_event_que.size_of_type = sizeof(nozzel_event_t);
	printing_event_que.copy = nozzel_event_copy;
	printing_event_que.buffer_size = PRINTING_EVENT_QUE_SIZE;
	printing_event_que.buffer = printing_event_buffer;

	display_decoder_init(&g_device_config, &nozzel_event_que, &printing_event_que);
	ferp_client_init(&g_device_config);
	printer_client_init(&g_device_config);
#endif

#ifdef FERP_PRINTER
	print_event_que.size_of_type = sizeof(print_event_t);
	print_event_que.copy = print_event_copy;
	print_event_que.buffer_size = PRINT_EVENT_QUE_SIZE;
	print_event_que.buffer = print_event_buffer;

	ret = printer_init(&g_device_config, &print_event_que);
	error_handler(ret);

	printer_start_server(&print_event_que);

	mutex_print_event_que = xSemaphoreCreateMutex();

#endif


#ifdef TEST_CLOUD_DATA_PUSH
	// nozzel_event_t ne = {0};
	// ne.total_price = 1345.32;
	// ne.unit_price = 367.50;
	// ne.volume_l = 1347.32/367.50;
	// logger.log(convertNozzelEvent_to_Json(&ne));
	// ferp_push_data(&ne);
#endif

	xTaskCreate(
		taskOne,          /* Task function. */
        "TaskOne",        /* String with name of task. */
        20*1024,            /* Stack size in bytes. */
        NULL,             /* Parameter passed as input of the task */
        1,                /* Priority of the task. */
        NULL);            /* Task handle. */

}


#ifdef FERP_PRINTER
		print_event_t pe;
#endif
nozzel_event_t ne = {0};
nozzel_event_t pne = {0};
void taskOne( void * parameter ){

	while(1){
		ret_t ret = ret_Success;

#ifdef FERP_COM
		while (Serial2.available()) {
			unsigned char b = Serial2.read();
			on_data_received(b);
			// logger.log("B=" + String(b));
		}

		display_decoder_process();

		if(que_getsize(&nozzel_event_que) > 0){
			if(que_pop(&nozzel_event_que, &ne) != -1){
				ret = ferp_push_data(&ne);
			}
		}

		if(que_getsize(&printing_event_que) > 0){
			if(que_pop(&printing_event_que, &pne) != -1){
				ret = printer_push_data(&pne);
			}
		}
#endif

#ifdef FERP_PRINTER
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
#endif

		static unsigned long ts_1;
		if(millis() - ts_1 > 1000*15){
			ts_1 = millis();
			struct tm after;
			get_time(&after);
		}

		vTaskDelay(10);
	}

    vTaskDelete( NULL );
}

void app_run(){
  	delay(10);

  	static unsigned long ts_1;
	if(millis() - ts_1 > 1000*15){
		ts_1 = millis();
	}
}
