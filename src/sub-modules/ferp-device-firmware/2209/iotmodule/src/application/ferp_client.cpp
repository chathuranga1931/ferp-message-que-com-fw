
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "error.h"
#include "nozzel_event.h"
#include "device_config.h"
#include "logger.h"

static device_configs_t * _device_configs;
// WiFiClientSecure secureclient;
#define MBEDTLS_TLS_DEFAULT_ALLOW_SHA1_IN_CERTIFICATES

extern String get_formatted_time(long tv_sec);

const char* root_ca= \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n";

ret_t ferp_client_init(device_configs_t * device_configs){

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

ret_t ferp_push_data(nozzel_event_t * n_event){

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

        // WiFiClientSecure client;
        // secureclient.setCACert(root_ca);
        HTTPClient http;
        http.begin(String(_device_configs->network.url), root_ca);
        http.addHeader("Authorization", "Basic M2RmMzE3NmMtOWQ2NC00YjQ4LTg2NTEtNWVmNTAzZWJhYWJiOjA4ZDNlNzYwLWE3Y2ItNDc5MC05NWNlLWNiMDIyNmZmNzYyOA==");
        http.addHeader("Content-Type", "application/json");
        String httpRequestJson = convertNozzelEvent_to_Json(n_event);
        logger.log("Message : " + httpRequestJson);
        int httpResponseCode = http.POST(httpRequestJson);
        ret = ret_Err_App_SubmitCloud;
        if(httpResponseCode == 201 || httpResponseCode == 200){
            StaticJsonBuffer<256> jsonBuffer;
		    JsonObject& root = jsonBuffer.parseObject(http.getString());
            if(root.containsKey("results")){
                JsonArray& results = root["results"].asArray();
                JsonObject& results_device = results[0];
                if(results_device.containsKey("status")){
                    String status = results_device["status"].as<String>();
                    if(status == "OK"){
                        logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " : Data sent success");
                        ret = ret_Success;
                    }
                    else{
                    }
                }
            }
        }
        else{
            logger.log("HTTP Response code: " + String(httpResponseCode) +" : " + http.getString());
        }
        // Free resources
        // http.setReuse(false);
        http.end();
        
    }while(false);

    return ret;
}

/*
{
    "points": [
        {
            "device": "3df3176c-9d64-4b48-8651-5ef503ebaabb",
            "time": "2022-10-26T11:44:55+05:31",
            "measurements": {
                "L": 2,
                "T": "100",
                "P": 800,
                "U": 400
            },
            "events": {
                "PUMPED": 1
            }
        }
    ]
}
*/
String convertNozzelEvent_to_Json(nozzel_event_t * n_event){

    ret_t ret = ret_Success;
    String strJson = "";

    do{
        if(n_event==nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("FC: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        StaticJsonBuffer<256> jsonBufferPoints;
        JsonObject& points = jsonBufferPoints.createObject();
        JsonArray& array = points.createNestedArray("points");
        JsonObject& root = array.createNestedObject();
        root["device"] = String(_device_configs->UUID);
        root["time"] = get_formatted_time(n_event->time_stamp);
        JsonObject& measurements = root.createNestedObject("measurements");
            measurements["L"] = n_event->volume_l;
            measurements["T"] = String(_device_configs->nozel_configs.fuel_type);
            measurements["P"] = n_event->total_price;
            measurements["U"] = n_event->unit_price;
        JsonObject& events = root.createNestedObject("events");
            events["PUMPED"] = 1;

        points.printTo(strJson);
    }while(false);

    return strJson;
}