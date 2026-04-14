
#include <Arduino.h>
#include <ArduinoJson.h>
// #include <Arduino_FreeRTOS.h>
// #include "semphr.h"

#include "error.h"
#include "print_event.h"
#include "device_config.h"
#include "logger.h"
#include "que.h"

static device_configs_t * _device_configs;
static AsyncWebServer * _server;
static que_t * _print_event_q;
SemaphoreHandle_t  mutex_print_event_que;

void handlePrintRequest(AsyncWebServerRequest *request);

ret_t printer_server_init(device_configs_t * device_configs){

    ret_t ret = ret_Success;

    do{
        if(device_configs == nullptr){
            logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret_Err_Gen_NullP));
            break;
        }

        _device_configs = device_configs;

        if(device_configs->async_server  == nullptr){
            logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret_Err_Gen_NullP));
            break;
        }

        _server = device_configs->async_server;

    }while(false);

    return ret;
}

// callback definition
void parseMyPageBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {

    static String strdata = "";

    if(index==0){
        strdata = "";
    }

    strdata = strdata + String(data, len);

    if(index + len >= total){
        logger.log(strdata);
        StaticJsonBuffer<512> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(strdata);
        if(root.containsKey("time")){
            String time = root["time"].as<String>();
            String nozzel_id = root["nozzel_id"].as<String>();
            JsonObject& measurements = root["measurements"].asObject();
                double volume_l = measurements["L"].as<double>();
                String fuel_type = measurements["T"].as<String>();
                double total_price = measurements["P"].as<double>();
                double unit_price = measurements["U"].as<double>();

            print_event_t pe;
            pe.fuel_type = fuel_type;
            pe.nozzel_id = nozzel_id;
            pe.time_stamp = time;
            pe.unit_price = unit_price;
            pe.volume_l = volume_l;
            pe.total_price = total_price;

		    if( xSemaphoreTake( mutex_print_event_que, ( TickType_t ) 75000 ) == pdTRUE ){
                que_push(_print_event_q, &pe);
                xSemaphoreGive(mutex_print_event_que);
                logger.log("Data pushed to Que : " + time);
            }
            else{
                logger.log("Err.. Que is busy, print lost : " + time);
            }

            // logger.log("Time : " + time);
            // logger.log("Nozzel ID : " + nozzel_id);
            // logger.log("Volume : " + String(volume_l));
            // logger.log("Total Price : " + String(total_price));
            // logger.log("Unit Price : " + String(unit_price));
            // logger.log("Fuel Type : " + fuel_type);
        }
    }
}

ret_t printer_start_server(que_t * print_event_q){

    ret_t ret = ret_Success;

    do{

        if(print_event_q == nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("PRNTR: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        _print_event_q = print_event_q;

        _server->on("/print", HTTP_POST, [](AsyncWebServerRequest *request) {
            request->send(200);
        }, nullptr, parseMyPageBody);

    }while(false);

    return ret;
}
