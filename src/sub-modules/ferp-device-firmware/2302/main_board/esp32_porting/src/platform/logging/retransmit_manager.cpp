#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SD.h>

#include "error.h"
#include "device_config.h"
#include "nozzel_event.h"
#include "retransmit_manager.h"
#include "logger.h"
#include "sd_api.h"
#include "sd_logger.h"

#define FILE_NAME_RETRANSMIT_STATUS         ("restransmitStatus")
#define FILE_NAME_RETRANSMIT_LIST           ("restransmitList")
#define FOLDER_NAME_FOR_RETRANSMIT          ("/Logs/")

/*
 * 0 - Today
 * 1 - Yesterday
 * 2 - Day before Yesterday
 */
static uint16_t total_at_0[NO_NOZZELS];
static uint16_t total_at_1[NO_NOZZELS];
static uint16_t total_at_2[NO_NOZZELS];

static uint16_t idx_at_0[NO_NOZZELS];
static uint16_t idx_at_1[NO_NOZZELS];
static uint16_t idx_at_2[NO_NOZZELS];

static String date_at_0, date_at_1, date_at_2;

static uint16_t idx_last_fetched[NO_NOZZELS] = {0};
static String date_last_fetched[NO_NOZZELS];

static device_configs_t * _device_configs;

static bool is_restransmitt_supported = false;

extern String get_date_as_string(long tv_sec);

ret_t _update_retransmit_current_status(uint8_t nozzel_id);
ret_t _read_retransmit_current_status(uint8_t nozzel_id);
ret_t _read_total_items_in_list(uint8_t nozzel_id);
ret_t _add_new_line_to_list(nozzel_event_t * ne, uint8_t nozzel_id);
ret_t _read_item_from_list(String date_at_0, uint16_t idx_at_0, nozzel_event_t * ne, uint8_t nozzel_id);
ret_t _get_lines_in_file(String file_path, uint16_t * nu_of_lines);

ret_t add_event_to_retransmit_list(nozzel_event_t * ne, uint8_t nozzel_id){

    ret_t ret = ret_Success;

    do{

        // logger.log("Adding new line to retransmit list");
        ret = _add_new_line_to_list(ne, nozzel_id);

    }while(false);

    return ret;
}

ret_t get_event_from_retransmit_list(nozzel_event_t * ne, uint8_t nozzel_id){

    ret_t ret = ret_Success;
    
    logger.log(String(nozzel_id));

    do{

        if(total_at_0[nozzel_id] > 0 && idx_at_0[nozzel_id] < total_at_0[nozzel_id]){
            logger.log("RTX: day_0");
            logger.log(String(total_at_0[nozzel_id]));
            ret = _read_item_from_list(date_at_0, idx_at_0[nozzel_id], ne, nozzel_id);
        }
        else if(total_at_1[nozzel_id] > 0 && idx_at_1[nozzel_id] < total_at_1[nozzel_id]){
            logger.log("RTX: day_1");
            logger.log(String(total_at_1[nozzel_id]));
            ret = _read_item_from_list(date_at_1, idx_at_1[nozzel_id], ne, nozzel_id);
        }
        else if(total_at_2[nozzel_id] > 0 && idx_at_2[nozzel_id] < total_at_2[nozzel_id]){
            logger.log("RTX: day_2" );
            logger.log(String(total_at_2[nozzel_id]));
            ret = _read_item_from_list(date_at_2, idx_at_2[nozzel_id], ne, nozzel_id);
        }
        else{
            ret = ret_err_App_Empty;
            break;
        }

    }while(false);

    return ret;
}

/*
 * Once this is called, the list maintained will be updated and write to
 * status file.
 */
ret_t on_retransmit_success(uint8_t nozzel_id){

    ret_t ret = ret_Success;

    do{
        if(date_last_fetched[nozzel_id] == date_at_0){
            idx_at_0[nozzel_id]++;
        }
        else if(date_last_fetched[nozzel_id] == date_at_1){
            idx_at_1[nozzel_id]++;
        }
        else if(date_last_fetched[nozzel_id] == date_at_2){
            idx_at_2[nozzel_id]++;
        }
        _update_retransmit_current_status(nozzel_id);

    }while(false);

    return ret;
}

ret_t update_dates(uint8_t nozzel_id){

    ret_t ret = ret_Success;

    do{
        struct timeval now;
        gettimeofday(&now, NULL);

        String today = date_at_0;
        date_at_0 = get_date_as_string(now.tv_sec);
        date_at_1 = get_date_as_string(now.tv_sec - 3600*24);
        date_at_2 = get_date_as_string(now.tv_sec - 3600*48);

        /* if the dates are not matched, that means the one day has passed
         * so the variables also should ajust according to that */
        if(today != date_at_0){

            if(today == date_at_1){ /* One day passed */
                total_at_2[nozzel_id] = total_at_1[nozzel_id];
                total_at_1[nozzel_id] = total_at_0[nozzel_id];
                total_at_0[nozzel_id] = 0;

                idx_at_2[nozzel_id] = idx_at_1[nozzel_id];
                idx_at_1[nozzel_id] = idx_at_0[nozzel_id];
                idx_at_0[nozzel_id] = 0;
            }
            else if(today == date_at_2){
                total_at_2[nozzel_id] = total_at_0[nozzel_id];
                total_at_1[nozzel_id] = 0;
                total_at_0[nozzel_id] = 0;

                idx_at_2[nozzel_id] = idx_at_0[nozzel_id];
                idx_at_1[nozzel_id] = 0;
                idx_at_0[nozzel_id] = 0;
            }
            else{
                total_at_2[nozzel_id] = 0;
                total_at_1[nozzel_id] = 0;
                total_at_0[nozzel_id] = 0;

                idx_at_2[nozzel_id] = 0;
                idx_at_1[nozzel_id] = 0;
                idx_at_0[nozzel_id] = 0;
            }

            _update_retransmit_current_status(nozzel_id);
        }

        logger.log("Today " + date_at_0);
        logger.log("Yesterday " + date_at_1);
        logger.log("Day Before Yesterday " + date_at_2);

    }while(false);

    return ret;
}

ret_t retransmit_manger_init(device_configs_t * device_configs){

    ret_t ret = ret_Success;

    do{

        if(device_configs != nullptr){
            _device_configs = device_configs;
        }
        else{
            logger.log("[" + String(__FILENAME__) + "]" + String(__LINE__));
			ret = ret_Err_Gen_NullP;
			break;
        }

        if(_device_configs->status.sd_card_status == sd_card_pending){
            logger.log("[" + String(__FILENAME__) + "]" + String(__LINE__));
            break;
        }

        is_restransmitt_supported = true;

        // Read status file from SD card and update variables, if not available
        // create file and assign them to 0

        /* Update the dates with current time */
        struct timeval now;
        gettimeofday(&now, NULL);
        date_at_0 = get_date_as_string(now.tv_sec);
        date_at_1 = get_date_as_string(now.tv_sec - 3600*24);
        date_at_2 = get_date_as_string(now.tv_sec - 3600*48);

        for(uint8_t i=0; i<NO_NOZZELS; i++){
            _read_retransmit_current_status(i);
            _read_total_items_in_list(i);
        }

    }while(false);

    return ret;
}

ret_t _update_retransmit_current_status(uint8_t nozzel_id){

    ret_t ret = ret_Success;

    do{
        logger.log("Writing retransmit status to SD card");

        if(!is_restransmitt_supported){
            logger.log(" Retransmit not supported ");
            break;
        }

        if(_device_configs->status.sd_card_status != sd_card_mounted){
            logger.log(" No SD Card ");
            break;
        }

        // Open retransmit_status json file
        StaticJsonBuffer<256> jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();

        root["date_at_0"] = date_at_0;
        root["idx_at_0"] = idx_at_0[nozzel_id];
        root["idx_at_1"] = idx_at_1[nozzel_id];
        root["idx_at_2"] = idx_at_2[nozzel_id];

        String jsonStr;
        root.printTo(jsonStr);
        logger.log(jsonStr);

        String filename = String(FILE_NAME_RETRANSMIT_STATUS) + "_" + String(nozzel_id) + ".json";
		String file_path = FOLDER_NAME_FOR_RETRANSMIT + filename;

        File file = SD.open(file_path, FILE_WRITE);
        if(!file){
            logger.log("Failed to open " + file_path + " for writing");
            return ret;
        }

        if(file.print(jsonStr)){
            // logger.log("" + file_path + " written");
        } else {
            logger.log("" + file_path + " failed");
        }
        file.close();

    }while(false);

    return ret;
}

ret_t _read_retransmit_current_status(uint8_t nozzel_id){

    ret_t ret = ret_Success;

    do{
        if(!is_restransmitt_supported){
            logger.log(" Retransmit not supported ");
            break;
        }

        //if sd card is not available, return
        if(_device_configs->status.sd_card_status == sd_card_pending){
            logger.log("SD card not supported ");
			break;
        }

        String filename = String(FILE_NAME_RETRANSMIT_STATUS) + String("_") + String(nozzel_id) + ".json";
		String file_path = FOLDER_NAME_FOR_RETRANSMIT + filename;
		if(!file_path.startsWith("/"))
			file_path = "/" + file_path;

        if(!SD.exists(file_path)){
            _update_retransmit_current_status(nozzel_id);
            logger.log("Failed to open " + file_path + " for reading");
			break;
        }

        File file = SD.open(file_path);
        if(!file){
            logger.log("Failed to open " + file_path + " for reading");
			break;
        }

        if(file.size() > 256){
            deleteFile(SD, file_path.c_str());
            _update_retransmit_current_status(nozzel_id);
        }

        String fileContent = "";
        // logger.log("Read from file: ");
        if(file.available()){
            fileContent = file.readString();
        }
        file.close();
		logger.log(fileContent);

        StaticJsonBuffer<256> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(fileContent);

        String date_at_0_from_file = "";
        uint16_t idx_at_0_from_file = 0;
        uint16_t idx_at_1_from_file = 0;
        uint16_t idx_at_2_from_file = 0;

        if(root.containsKey("date_at_0")){
            date_at_0_from_file = root["date_at_0"].as<String>();
        }

        if(root.containsKey("idx_at_0")){
            idx_at_0_from_file = root["idx_at_0"].as<int>();
        }
        else{
            idx_at_0_from_file = 0;
        }

        if(root.containsKey("idx_at_1")){
            idx_at_1_from_file = root["idx_at_1"].as<int>();
        }
        else{
            idx_at_1_from_file = 0;
        }

        if(root.containsKey("idx_at_2")){
            idx_at_2_from_file = root["idx_at_2"].as<int>();
        }
        else{
            idx_at_2_from_file = 0;
        }

        /* dates should be optained before calling this function */
        /* Means, the date has changed, so the indexes are not vailid */
        if(date_at_0_from_file == date_at_0){

            idx_at_0[nozzel_id] = idx_at_0_from_file;
            idx_at_1[nozzel_id] = idx_at_1_from_file;
            idx_at_2[nozzel_id] = idx_at_2_from_file;
        }
        else if(date_at_0_from_file == date_at_1){

            idx_at_0[nozzel_id] = 0;
            idx_at_1[nozzel_id] = idx_at_0_from_file;
            idx_at_2[nozzel_id] = idx_at_1_from_file;
        }
        else if(date_at_0_from_file == date_at_2){

            idx_at_0[nozzel_id] = 0;
            idx_at_1[nozzel_id] = 0;
            idx_at_2[nozzel_id] = idx_at_0_from_file;
        }
        else{
            idx_at_0[nozzel_id] = 0;
            idx_at_1[nozzel_id] = 0;
            idx_at_2[nozzel_id] = 0;
        }

        _update_retransmit_current_status(nozzel_id);

        logger.log(String(nozzel_id) + "ReTX Idx 0 " + String(idx_at_0[nozzel_id]));
        logger.log(String(nozzel_id) + "ReTX Idx 1 " + String(idx_at_1[nozzel_id]));
        logger.log(String(nozzel_id) + "ReTX Idx 2 " + String(idx_at_2[nozzel_id]));

    }while(false);

    return ret;
}

ret_t _get_lines_in_file(String file_path, uint16_t * nu_of_lines){

    ret_t ret = ret_Success;

    do{
        *nu_of_lines = 0;

        if(!SD.exists(file_path)){
            logger.log("No file available for reading");
			break;
        }

        File file = SD.open(file_path, FILE_READ);
        if(!file){
            logger.log("Failed to open " + file_path + " for writing");
            break;
        }
        // logger.log(" Opened " + file_path);

        bool available = false;
        do{
            available = false;
            String readStr = file.readStringUntil('\n');
            if(readStr.length() > 0){
                available = true;
                (*nu_of_lines)++;
            }
        }while(available);
        file.close();

    }while(false);
    return ret;
}

/* update the global variable */
ret_t _read_total_items_in_list(uint8_t nozzel_id){

    ret_t ret = ret_Success;

    do{
        if(!is_restransmitt_supported){
            logger.log(" Retransmit not supported ");
            break;
        }

        total_at_0[nozzel_id] = 0;
        total_at_1[nozzel_id] = 0;
        total_at_2[nozzel_id] = 0;

        if(_device_configs->status.sd_card_status != sd_card_mounted){
            logger.log(" No SD Card ");
            break;
        }

        String filename = FILE_NAME_RETRANSMIT_LIST + date_at_0 + "_" + String(nozzel_id) + ".json";
		String file_path = FOLDER_NAME_FOR_RETRANSMIT + filename;
        uint16_t nu_of_lines = 0;
        _get_lines_in_file(file_path, &nu_of_lines);
        total_at_0[nozzel_id] = nu_of_lines;

        filename = FILE_NAME_RETRANSMIT_LIST + date_at_1 + "_" + String(nozzel_id) + ".json";
		file_path = FOLDER_NAME_FOR_RETRANSMIT + filename;
        nu_of_lines = 0;
        _get_lines_in_file(file_path, &nu_of_lines);
        total_at_1[nozzel_id] = nu_of_lines;

        filename = FILE_NAME_RETRANSMIT_LIST + date_at_2 + "_" + String(nozzel_id) + ".json";
		file_path = FOLDER_NAME_FOR_RETRANSMIT + filename;
        nu_of_lines = 0;
        _get_lines_in_file(file_path, &nu_of_lines);
        total_at_2[nozzel_id] = nu_of_lines;

        logger.log("ReTX Total Today " + String(total_at_0[nozzel_id]));
        logger.log("ReTX Total Yesterday " + String(total_at_1[nozzel_id]));
        logger.log("ReTX Total Day before Yesterday " + String(total_at_2[nozzel_id]));

    }while(false);

    return ret;
}

ret_t _add_new_line_to_list(nozzel_event_t * ne, uint8_t nozzel_id){

    ret_t ret = ret_Success;

    do{
        if(!is_restransmitt_supported){
            logger.log(" Retransmit not supported ");
            break;
        }

        if(_device_configs->status.sd_card_status != sd_card_mounted){
            logger.log(" No SD Card ");
            break;
        }

        String filename = FILE_NAME_RETRANSMIT_LIST + date_at_0 + "_" + String(nozzel_id) + ".json";
		String filePath = FOLDER_NAME_FOR_RETRANSMIT + filename;
        File file = SD.open(filePath, FILE_APPEND);
        if(!file){
            logger.log("Failed to open " + filePath +" for writing");
            // write_debug_log("Failed to open " + filePath +" for writing");
            return ret;
        }
        // write_debug_log("Write retransmit event to file : " + filePath);
        logger.log("Write retransmit event to file : " + filePath);

        String data_line;
        StaticJsonBuffer<512> jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        root["ts"] = ne->time_stamp;
        root["up"] = ne->unit_price;
        root["vol"] = ne->volume_l;
        root["tot"] = ne->total_price;

        root.printTo(data_line);
        file.print(data_line + "\n");
        total_at_0[nozzel_id]++;

        // logger.log("Added line to liset " + data_line);
        // logger.log("Availabel Lines " + String(total_at_0));

        file.close();

    }while(false);
    return ret;
}

ret_t _read_item_from_list(String date, uint16_t idx, nozzel_event_t * ne, uint8_t nozzel_id){

    ret_t ret = ret_err_App_Empty;

    do{
        logger.log("ReTX N" + String(nozzel_id) + " IDX=" + String(idx) + " at " + date);

        if(!is_restransmitt_supported){
            logger.log(" Retransmit not supported ");
            break;
        }

        if(_device_configs->status.sd_card_status != sd_card_mounted){
            logger.log(" No SD Card ");
            break;
        }

        if(ne == nullptr){
            logger.log(" NE is null pointer ");
            break;
        }

        String filename = FILE_NAME_RETRANSMIT_LIST + date + "_" + String(nozzel_id) + ".json";
		String filePath = "/Logs/" + filename;

        if(!SD.exists(filePath)){
            logger.log("No " + filePath +" available for reading");
			break;
        }

        File file = SD.open(filePath, FILE_READ);
        if(!file){
            logger.log("Failed to open " + filePath +" for reading");
            break;
        }
        logger.log("File Opened " + filePath);

        bool available = false;
        uint16_t line_number = 0;
        String read_str_line;
        bool line_found = false;
        do{
            available = false;
            read_str_line = file.readStringUntil('\n');
            if(line_number == idx){
                line_found = true;
                break;
            }

            if(read_str_line.length() > 0){
                available = true;
                line_number++;
            }
        }while(available);
        file.close();

        logger.log("Fetched data at Line " + String(idx) + " = " + read_str_line);

        idx_last_fetched[nozzel_id] = idx;
        date_last_fetched[nozzel_id] = date;

        StaticJsonBuffer<256> json_buffer;
        JsonObject& root = json_buffer.parseObject(read_str_line);

        if(root.containsKey("ts")){
            ne->time_stamp = root["ts"].as<long>();
        }
        else{
            ret = ret_err_App_Empty;
            logger.log("ts");
            break;
        }

        if(root.containsKey("up")){
            ne->unit_price = root["up"].as<double>();
        }
        else{
            ret = ret_err_App_Empty;
            logger.log("up");
            break;
        }

        if(root.containsKey("vol")){
            ne->volume_l = root["vol"].as<double>();
        }
        else{
            ret = ret_err_App_Empty;
            logger.log("vol");
            break;
        }

        if(root.containsKey("tot")){
            ne->total_price = root["tot"].as<double>();
        }
        else{
            ret = ret_err_App_Empty;
            logger.log("tot");
            break;
        }

    }while(false);
    
    return ret;
}



