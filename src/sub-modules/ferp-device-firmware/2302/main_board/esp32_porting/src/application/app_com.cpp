

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
#include "error.h"

#include "utility/que.h"
#include "nozzel_event.h"
#include "board.h"

#include "sd_api.h"
#include "sd_logger.h"
#include "display_tap.h"

#include "wifi_manager.h"
#include "date_time_manger.h"
#include "retransmit_manager.h"
#include "internet_connectivity.h"
#include "display1_decoder.h"
#include "led_cue.h"
#include "ferp_client.h"
#include "printer_client.h"
#include "app_com.h"

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
/*===================================================.===================================================*/
/*                                            PRIVATE VARIABLES                                          */
/*=======================================================================================================*/
nozzel_event_t nozzel_event_buffer0[NOZZEL_EVENT_QUE_SIZE];
nozzel_event_t nozzel_event_buffer1[NOZZEL_EVENT_QUE_SIZE];
que_t nozzel_event_que[NO_NOZZELS] = {0};
nozzel_event_t printing_event_buffer0[PRINTING_EVENT_QUE_SIZE];
nozzel_event_t printing_event_buffer1[PRINTING_EVENT_QUE_SIZE];
que_t printing_event_que[NO_NOZZELS] = {0};
nozzel_event_t ne = {0};
nozzel_event_t pne = {0};
static internet_status_t prev_internet_status = internet_disconnected;
/*===================================================.===================================================*/
/*                                      PRIVATE FUNCTION PROTOTYPES                                      */
/*=======================================================================================================*/
void fuel_event_display_1(display_data_v2_t data);
void fuel_event_display_2(display_data_v2_t data);
nozzel_event_t * get_event_buffer_name(uint8_t buffer_id);
nozzel_event_t * get_printing_buffer_name(uint8_t buffer_id);

/*===================================================.===================================================*/
/*                                      PUBLIC FUNCTIONS DEFINITIONS                                     */
/*=======================================================================================================*/
void app_com_init(){
	ret_t ret = ret_Success;
	logger.log("FERP-COM");
	
	led_init();
	led_process();

	led_green1_blink_delay(0xFFFF);
	led_process();

    sd_init(&g_device_config);
	sd_logger_init(&g_device_config);
	retransmit_manger_init(&g_device_config);

	write_note("Device Starting...");
	write_note("SD Card Status : OK");

	if(g_device_config.status.rtc_status == rtc_available){
		write_note("[RTC]: Connected successfully");
	}
	else{
		write_error("[RTC]: Failed to connect");
	}

	if(g_device_config.status.date_time_status == date_time_sync_failed){
		write_error("[Date Time]: Time sync failed");
	}

	for(int i =0; i<NO_NOZZELS; i++){
		nozzel_event_que[i].size_of_type = sizeof(nozzel_event_t);
		nozzel_event_que[i].copy = nozzel_event_copy;
		nozzel_event_que[i].buffer_size = NOZZEL_EVENT_QUE_SIZE;
		nozzel_event_que[i].buffer = get_event_buffer_name(i);

		printing_event_que[i].size_of_type = sizeof(nozzel_event_t);
		printing_event_que[i].copy = nozzel_event_copy;
		printing_event_que[i].buffer_size = PRINTING_EVENT_QUE_SIZE;
		printing_event_que[i].buffer = get_printing_buffer_name(i);
	}

	display_decoder_init(&g_device_config);
    init_display_tap(fuel_event_display_1, fuel_event_display_2);
	ferp_client_init(&g_device_config);
	printer_client_init(&g_device_config);

}

void app_com_run(){

	ret_t ret = ret_Success;
	display_tap();

	static uint8_t nozzel_id = 0;
	nozzel_id++;
	if(nozzel_id >= NO_NOZZELS) nozzel_id = 0;

	// logger.log("nozzel_id = " + String(nozzel_id));

	ret = ret_Success;
	if(que_getsize(&(nozzel_event_que[nozzel_id])) > 0){
		if(que_pop(&(nozzel_event_que[nozzel_id]), &ne) != -1){
			g_device_config.nozzel_stat[nozzel_id].cloud_pushed_event_count ++;
			ret = ferp_push_data(&ne, nozzel_id);
			String sent_status = ret == ret_Success ? "P" : "F";
			logger.log("FERP_PUSH_DATA :  " + String(nozzel_id) +
										"," + String(ne.time_stamp) +
										"," + String(ne.unit_price) +
										"," + String(ne.volume_l, 3) +
										"," + String(ne.total_price) +
										"," + sent_status);
			write_pumped_event_log(String(ne.time_stamp) +
										"," + String(ne.unit_price) +
										"," + String(ne.volume_l, 3) +
										"," + String(ne.total_price) +
										"," + sent_status, nozzel_id);

			if(ret != ret_Success){
				write_cloud_failed_event_log(String(ne.time_stamp) +
										"," + String(ne.unit_price) +
										"," + String(ne.volume_l, 3) +
										"," + String(ne.total_price), nozzel_id);
				add_event_to_retransmit_list(&ne, nozzel_id);
				g_device_config.nozzel_stat[nozzel_id].cloud_fail_event_count ++;
			}
		}
	}
	else{

		if((g_device_config.status.internet_status == internet_connected) &&
			(millis() - g_device_config.nozzel_data[nozzel_id].ts_last_cloud_push_failed > CONFIG_CLOUD_PUSH_RETRY_DELAY_AFTER_LAST_FAIL) ){
			if(get_event_from_retransmit_list(&ne, nozzel_id) == ret_Success){

				ret = ferp_push_data(&ne, nozzel_id);
				g_device_config.nozzel_stat[nozzel_id].retransmitted_event_count++;
				String sent_status = ret == ret_Success ? "RP" : "RF";
				static long last_retried_time_stamp = 0;

				if((ret == ret_Success) || (last_retried_time_stamp != ne.time_stamp)){
					last_retried_time_stamp = ne.time_stamp;
					logger.log("FERP_PUSH_DATA (RTX):  " + String(nozzel_id) +
												"," + String(ne.time_stamp) +
												"," + String(ne.unit_price) +
												"," + String(ne.volume_l, 3) +
												"," + String(ne.total_price) +
												"," + sent_status);
					write_pumped_event_log(String(ne.time_stamp) +
												"," + String(ne.unit_price) +
												"," + String(ne.volume_l, 3) +
												"," + String(ne.total_price) +
												"," + sent_status, nozzel_id);
				}

				if(ret == ret_Success){
					on_retransmit_success(nozzel_id);
				}
				else{
					g_device_config.nozzel_stat[nozzel_id].retransmit_failed_event_count++;
				}
			}
		}
	}

	if(que_getsize(&(printing_event_que[nozzel_id])) > 0){
		if(que_pop(&(printing_event_que[nozzel_id]), &pne) != -1){
			ret = printer_push_data(&pne, nozzel_id);
		}
	}

	if(g_device_config.status.wifi_status == wifi_status_ap_mode){
		led_green1_blink_delay(150);
		led_red_blink_delay(0);
	}
	else if(g_device_config.status.wifi_status == wifi_status_connected){
		led_green1_blink_delay(500);
		led_red_blink_delay(0);
	}
	else if(g_device_config.status.wifi_status == wifi_status_disconnected){
		led_red_blink_delay(1000);
	}

	/* Reset to default logic */
	unsigned char default_btn_value = digitalRead(GPIO_BTN_DEFAULT);
	static unsigned long default_btn_press_count_ms = 0;
	static unsigned long ts_last_default_btn_press = 0;
	if(default_btn_value == 0){
		default_btn_press_count_ms = 0;
	}
	else{
		// logger.log("Button Pressed.... ");
		if(default_btn_press_count_ms == 0){
			default_btn_press_count_ms = 1;
			ts_last_default_btn_press = millis();
		}
		else{
			default_btn_press_count_ms += millis() - ts_last_default_btn_press;
			ts_last_default_btn_press = millis();
		}
	}

	if(default_btn_press_count_ms > 5000){
		logger.log("Config reset to defaults.... ");
		write_note("Restarting to default through button");

		default_btn_press_count_ms = 0;
		led_green1_blink_delay(0xFFFF);
		led_green2_blink_delay(0xFFFF);
		led_red_blink_delay(0xFFFF);
		led_process();

		load_defult_configurations();
		delay(2000);

		led_green1_blink_delay(0);
		led_green2_blink_delay(0);
		led_red_blink_delay(0);
		led_process();

		delay(1000);

		ESP.restart();
	}

	static unsigned long ts_5mins;
	if(millis() - ts_5mins > 300000){
		ts_5mins = millis();

		/* update file list every 300 seconds. This is not needed in final execusion.
			* but better to have for debugging */
		listDir(SD, "/", 2, true);
		listDir(SPIFFS, "/", 2, true);


		/* Log files has pretext of date, this will check today's date time
			* if the prefix not matched to today's date, it will reset the variable
			* So, next time it it logs values has been updated. */
		create_log_file();

		/* Retransmit system uses files to store data for last 2 days. 
			* to monitoring the date change this function should be called */
		for(int i=0; i<NO_NOZZELS; i++){
			update_dates(i);
		}


		/* Check internet connectivity, if WiFi is connected only, if wifi is not
			* connected, internet alwasy not connected. */
		check_internet(&g_device_config);
		if(g_device_config.status.wifi_status == wifi_status_connected){
			if(g_device_config.status.internet_status != internet_connected){
				write_error("Internet: Disconnected");
			}
			else if(g_device_config.status.internet_status == internet_connected && prev_internet_status != internet_connected){
				write_note("Internet: Connected");
			}
		}

		/* if Time sync is failed, retry to sync time with RTC or from NTP */
		if(g_device_config.status.date_time_status == date_time_sync_failed){
			if(g_device_config.status.internet_status == internet_connected){
				date_time_sync();
			}
			else if(g_device_config.status.rtc_status == rtc_available){
				date_time_sync();
			}
		}
	}

	// static unsigned long ts_30sec;
	// if(millis() - ts_30sec > 30000){
	// 	#ifdef TEST_DEVICE
	// 		struct timeval now;
	// 		static double liters = 0;
	// 		liters += 0.5;
	// 		gettimeofday(&now, NULL);
	// 		nozzel_event_t netmp = {0};
	// 		netmp.total_price = liters * 370.0;
	// 		netmp.unit_price = 370.00;
	// 		netmp.volume_l = liters;
	// 		netmp.time_stamp = now.tv_sec;
	// 		que_push(&nozzel_event_que[nozzel_id], &netmp);
	// 	#endif
	// }

	static unsigned long ts_1mins;
	if(millis() - ts_1mins > 60000){
		ts_1mins = millis();
		ferp_send_heart_beat();
	}

	led_process();
}


/*===================================================.===================================================*/
/*                                      PRIVATE FUNCTIONS DEFINITIONS                                    */
/*=======================================================================================================*/
/**
* @name		:
* @brief	:
*
* @param	:
* @return	:
**/
nozzel_event_t * get_event_buffer_name(uint8_t buffer_id)
{
	switch(buffer_id){
		case 0:
			return nozzel_event_buffer0;
		break;
		case 1:
			return nozzel_event_buffer1;
		break;
		default:
		break;
	}
}

nozzel_event_t * get_printing_buffer_name(uint8_t buffer_id)
{
	switch(buffer_id){
		case 0:
			return printing_event_buffer0;
		break;
		case 1:
			return printing_event_buffer1;
		break;
		default:
		break;
	}
}

#define START_STOP_SAMPLE_MASK 	((uint8_t)(0b11110000))
void fuel_event_display_1(display_data_v2_t data){

	static bool prev_start_stop = false;
	static uint8_t start_stop_sampler = 0x0;
	static bool verified_start_stop = false;

	start_stop_sampler = start_stop_sampler << 1;
	start_stop_sampler |= ((0x01) & data.flags.start_stop);

	/* check last 4 consequetive start stop flags are stops */
	if((data.flags.start_stop == 0) && ((start_stop_sampler | START_STOP_SAMPLE_MASK) == START_STOP_SAMPLE_MASK)){
		verified_start_stop = false;
	}
	else if(data.flags.start_stop == 1){
		if( (start_stop_sampler & ((uint8_t)(~(START_STOP_SAMPLE_MASK)))) == ((uint8_t)(~(START_STOP_SAMPLE_MASK))) ) {
			verified_start_stop = true;
		}
	}
	else{
		verified_start_stop = verified_start_stop;
	}

	display_data_t tmp;
	tmp.select_l = data.flags.select_l;
	tmp.select_p = data.flags.select_p;
	tmp.total_price = data.total_price * 0.01;
	tmp.start_stop = verified_start_stop;
	tmp.volume_l = data.volume_l * 0.001;
	tmp.unit_price = data.unit_price * 0.01;
    // Serial.println("display 1 = " + String(data.unit_price / 100.0) + ", " + String(data.total_price / 100.0) + ", " + String(data.volume_l / 1000.0) + ", " + (data.flags.start_stop ? "start" : "stop") + ", p=" + (data.flags.select_p ? "1" : "0") + " l=" + (data.flags.select_l ? "1" : "0"));
    
	write_display_tap_data("N1 = U=" + String(data.unit_price) + "," +
		"V=" + String(data.volume_l) + "," +
		"T=" + String(data.total_price) + "," +
		"L=" + String(data.flags.select_l) + "," +
		"P=" + String(data.flags.select_p) + "," +
		"S=" + String(data.flags.start_stop)
	);
	
	display_decoder_process(tmp, 0, &(nozzel_event_que[0]), &prev_start_stop);	
}

void fuel_event_display_2(display_data_v2_t data){

	static bool prev_start_stop = false;
	static uint8_t start_stop_sampler = 0x0;
	static bool verified_start_stop = prev_start_stop;

	start_stop_sampler = start_stop_sampler << 1;
	start_stop_sampler |= ((0x01) & data.flags.start_stop);

	/* check last 4 consequetive start stop flags are stops */
	if((data.flags.start_stop == 0) && ((start_stop_sampler | START_STOP_SAMPLE_MASK) == START_STOP_SAMPLE_MASK)){
		verified_start_stop = false;
	}
	else if(data.flags.start_stop == 1){
		if( (start_stop_sampler & ((uint8_t)(~(START_STOP_SAMPLE_MASK)))) == ((uint8_t)(~(START_STOP_SAMPLE_MASK))) ) {
			verified_start_stop = true;
		}
	}
	else{
		verified_start_stop = verified_start_stop;
	}

	display_data_t tmp;
	tmp.select_l = data.flags.select_l;
	tmp.select_p = data.flags.select_p;
	tmp.total_price = data.total_price * 0.01;
	tmp.start_stop = verified_start_stop;
	tmp.volume_l = data.volume_l * 0.001;
	tmp.unit_price = data.unit_price * 0.01;
    // Serial.println("display 2 = " + String(data.unit_price / 100.0) + ", " + String(data.total_price / 100.0) + ", " + String(data.volume_l / 1000.0) + ", " + (data.flags.start_stop ? "start" : "stop") + ", p=" + (data.flags.select_p ? "1" : "0") + " l=" + (data.flags.select_l ? "1" : "0"));
    
	write_display_tap_data("N2 = U=" + String(data.unit_price) + "," +
		"V=" + String(data.volume_l) + "," +
		"T=" + String(data.total_price) + "," +
		"L=" + String(data.flags.select_l) + "," +
		"P=" + String(data.flags.select_p) + "," +
		"S=" + String(data.flags.start_stop)
	);
	display_decoder_process(tmp, 1, &(nozzel_event_que[1]), &prev_start_stop);
}