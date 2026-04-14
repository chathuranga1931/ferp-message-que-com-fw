#include <Arduino.h>

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "device_config.h"
#include "logger.h"
#include "error.h"
#include "sd_logger.h"
#include "sd_api.h"

extern String get_date_as_string(long tv_sec);
extern String get_formatted_time(long tv_sec);

static device_configs_t * _device_configs = nullptr;
static String today_log_file = "2022-12-01";
static int log_file_id = 1;
static bool is_logging_available = false;

void find_log_file_idx();

ret_t sd_logger_init(device_configs_t * device_configs){

  ret_t ret = ret_Success;
	do{
        if(device_configs != nullptr){
            _device_configs = device_configs;
        }
        else{
            logger.log("Fundamental error, check source...");
            delay(5000);
            while(1);
        }

		if(_device_configs->status.sd_card_status != sd_card_mounted){
            logger.log("No SD card, No logging...");
			return ret_Err_Hdware;
		}

        is_logging_available = true;
		createDir(SD, "/Logs");

        /* Update date to today */
        struct timeval now;
        gettimeofday(&now, NULL);
        today_log_file = get_date_as_string(now.tv_sec);

        find_log_file_idx();
        logger.log("Log file index (After) = " + String(log_file_id));

	}while(false);

	return ret;
}

void find_log_file_idx(){

    log_file_id = 1;
    bool file_found = false;
    do{

        String file_path = "/Logs/" + today_log_file + "_" +String(log_file_id) + ".log";
        if(!SD.exists(file_path)){
            logger.log("Log file index = " + String(log_file_id));
            break;
        }

        log_file_id++;
        if(log_file_id >= 100){
            log_file_id = 100;
            file_path = "/Logs/" + today_log_file + "_" +String(log_file_id) + ".log";
            deleteFile(SD, file_path.c_str());
            break;
        }

    }while(true);
}

ret_t create_log_file(){

    ret_t ret = ret_Success;

    do{
        struct timeval now;
        gettimeofday(&now, NULL);
        String date = get_date_as_string(now.tv_sec);

        if(today_log_file != date){
            today_log_file = date;
            log_file_id = 1;
        }

    }while(false);

    return ret;
}

void write_error(String log_message){
    write_log( "[ERROR]" + log_message);
}

void write_note(String log_message){
    write_log( "[NOTE ]" + log_message);
}


void write_event(String log_message){
    write_log( "[EVENT]" + log_message);
}

void write_log(String log_message){

    if(!is_logging_available) return;

    struct timeval now;
    gettimeofday(&now, NULL);
    String file_path = "/Logs/" + today_log_file + "_" + String(log_file_id) + ".log";

    static unsigned int delcount = 0;
    if(delcount >= 400){
        delcount = 0;
        today_log_file = get_date_as_string(now.tv_sec);
        log_file_id++;
    }

    delcount++;

    String message = get_formatted_time(now.tv_sec) + " " + log_message + "\r\n";
    appendFile(SD, file_path.c_str(), message.c_str());
}

void write_pumped_event_log(String log_message, uint8_t nozzel_id){
	if(!is_logging_available) return;

    // struct timeval now;
    // gettimeofday(&now, NULL);
    // String file_path = "/Logs/event-pumped-" + String(nozzel_id) + "-" + today_log_file + ".log";
    // String message = get_formatted_time(now.tv_sec) + " " + log_message + "\r\n";
    // appendFile(SD, file_path.c_str(), message.c_str());

    write_event("PUMPED,NID=" + String(nozzel_id) + "," + log_message);
}

void write_cloud_failed_event_log(String log_message, uint8_t nozzel_id){
	if(!is_logging_available) return;

    // struct timeval now;
    // gettimeofday(&now, NULL);
    // String file_path = "/Logs/event-cloudfailed-" + String(nozzel_id) + "-" + today_log_file + ".log";
    // String message = get_formatted_time(now.tv_sec) + " " + log_message + "\r\n";
    // appendFile(SD, file_path.c_str(), message.c_str());
    
    write_event("CLOUDFAILED,NID=" + String(nozzel_id) + "," + log_message);
}

// void write_buffer_log(unsigned char c){
//     if(!is_logging_available) return;

//     struct timeval now;
//     gettimeofday(&now, NULL);
//     String file_path = "/Logs/serial-data.log";

//     static unsigned int delcount = 0;
//     if(delcount == 0 || delcount >= 1000){
//         delcount = 0;
//         deleteFile(SD, file_path.c_str());
//     }

//     delcount++;
//     String message = " 0x" + String(c,HEX);
//     appendFile(SD, file_path.c_str(), message.c_str());
// }

void write_buffer_log(unsigned char * BuffA, unsigned char * BuffB, unsigned int length){
    if(!is_logging_available) return;

    struct timeval now;
    gettimeofday(&now, NULL);

    String file_path = "/Logs/serial-data.log";

    String bufferA, bufferB;
    for(int i=0; i< length; i++){
        bufferA += "0x" + String(BuffA[i], HEX) + " ";
        bufferB += "0x" + String(BuffB[i], HEX) + " ";
    }

    static unsigned int delcount = 0;
    if(delcount == 0 || delcount >= 100){
        delcount = 0;
        deleteFile(SD, file_path.c_str());
    }

    delcount ++;
    String message = get_formatted_time(now.tv_sec) + " " + bufferA + "," + bufferB + "\r\n";
    appendFile(SD, file_path.c_str(), message.c_str());
}


void write_display_tap_data(String data){

    if(!is_logging_available) return;

    static uint8_t file_swap = 1;
    struct timeval now;
    gettimeofday(&now, NULL);

    String file_path = "/Logs/display_tap_" + String(file_swap) + ".log";

    static unsigned int delcount = 0;
    if(delcount == 0 || delcount >= 500){
        delcount = 0;

        file_swap++;
        if(file_swap > 2){
            file_swap = 1;
        }
        file_path = "/Logs/display_tap_" + String(file_swap) + ".log";
        deleteFile(SD, file_path.c_str());
    }

    delcount ++;
    char tmp_str[10];
    sprintf(tmp_str, "[%04d] ", delcount);
    String message = String(tmp_str) + get_formatted_time(now.tv_sec) + " " + data + "\r\n";
    appendFile(SD, file_path.c_str(), message.c_str());
}

void write_debug_log(String msg){
    if(!is_logging_available) return;

    struct timeval now;
    gettimeofday(&now, NULL);

    String file_path = "/Logs/debug.log";

    static unsigned int delcount = 0;
    if(delcount == 0 || delcount >= 500){
        delcount = 0;
        deleteFile(SD, file_path.c_str());
    }

    delcount ++;
    char tmp_str[10];
    sprintf(tmp_str, "[%04d] ", delcount);
    String message = String(tmp_str) + get_formatted_time(now.tv_sec) + " " + msg + "\r\n";
    appendFile(SD, file_path.c_str(), message.c_str());
}