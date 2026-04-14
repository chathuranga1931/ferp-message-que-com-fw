
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
// #include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "error.h"
#include "nozzel_event.h"
#include "device_config.h"
#include "logger.h"
#include "sd_logger.h"


static device_configs_t * _device_configs;

extern String get_formatted_time(long tv_sec);

ret_t printer_client_init(device_configs_t * device_configs){

    ret_t ret = ret_Success;

    do{
        if(device_configs == nullptr){
            logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret_Err_Gen_NullP));
            break;
        }

        _device_configs = device_configs;

    }while(false);

    return ret;
}

String convertPrintingEvent_to_Json(nozzel_event_t * n_event, uint8_t nozzel_id){

    ret_t ret = ret_Success;
    String strJson = "";

    do{
        if(n_event==nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        StaticJsonBuffer<256> jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();
        root["time"] = get_formatted_time(n_event->time_stamp);
        root["nozzel_id"] = _device_configs->nozel_configs[nozzel_id].nozzel_id;
        JsonObject& measurements = root.createNestedObject("measurements");
            measurements["L"] = n_event->volume_l;
            measurements["T"] = _device_configs->nozel_configs[nozzel_id].fuel_type_str;
            measurements["P"] = n_event->total_price;
            measurements["U"] = n_event->unit_price;
        root.printTo(strJson);
    }while(false);

    return strJson;
}

// static long last_n_event_time_stamp; /* This is to block multiple print request within 5 seconds */
// static unsigned long last_print_time_stamp;
ret_t printer_push_data(nozzel_event_t * n_event, uint8_t nozzel_id){

    ret_t ret = ret_Success;

    do{
        if(_device_configs == nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        if(!_device_configs->status.internet_status){
            ret = ret_Err_App_NoInternet;
            logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        // if(last_print_time_stamp == n_event->time_stamp && (millis() - last_print_time_stamp < 5000)){
        //     ret = ret_Err_App_InvalidPrintEnvet;
        //     logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
        //     break;
        // }

        // last_print_time_stamp = n_event->time_stamp;

        // WiFiClientSecure client;
        // secureclient.setCACert(root_ca);
        HTTPClient http;
        http.addHeader("Content-Type", "application/json");
        http.begin(String(_device_configs->printer.url));
        String httpRequestJson = convertPrintingEvent_to_Json(n_event, nozzel_id);
        // logger.log("Printing Message : " + httpRequestJson);
        int httpResponseCode = http.POST(httpRequestJson);
        ret = ret_Err_App_SubmitPrinter;
        if(httpResponseCode == 201 || httpResponseCode == 200){
            // logger.log("Printing success : " + httpRequestJson);
            write_note("FERP-Print: Printing Success" );
        }
        else{
            // logger.log("Printing failed : " + httpRequestJson);
            write_error("FERP-Print: Printing Failed" );
        }
        http.end();

    }while(false);

    return ret;
}