

#include <Arduino.h>
#include <WiFi.h>

#include "device_config.h"
#include "logger.h"
#include "error.h"

extern void check_internet(device_configs_t * device_configs);


bool soft_apmode = false;

// Replace with your network credentials
const char* ssid     = "FERP";
/* Password will be the mac address of the device */

// Replace with your network credentials
// const char* ssid_station    = "Dialog 4G 528";
// const char* password_station = "3e3C25e1";
// const char* ssid_station    = "Dialog 4G";
// const char* password_station = "FJ9R15EMNL9";
const char* ssid_station    = "HUAWEI-B310-98DE";
const char* password_station = "CHATHU**123";

static device_configs_t * _device_configs =  nullptr;

ret_t wifi_init(device_configs_t* device_configs){

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

        WiFi.begin(_device_configs->wifi.ssid, _device_configs->wifi.password);
        logger.log("Connecting to ");
        logger.log(_device_configs->wifi.ssid);
        _device_configs->status.wifi_status = wifi_status_connected;
        unsigned long ts = millis();
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            logger.log_(".");
            if(millis() - ts > (_device_configs->wifi.wait_time_s * 1000)){
                _device_configs->status.wifi_status = wifi_status_pending;
                break;
            }
        }

        strcpy(_device_configs->mac_address, WiFi.macAddress().c_str());

        if(_device_configs->status.wifi_status == wifi_status_connected){

            logger.log("");
            logger.log("WiFi connected.");
            logger.log("IP address: ");
            logger.log_header();
            logger.log_("IP address: ");
            logger.log_(String(WiFi.localIP().toString()));
            logger.log_footer();
            Serial.println(WiFi.localIP());
        }
        else{
            // Connect to Wi-Fi network with SSID and password
            logger.log("Setting AP (Access Point)…");

            // Remove the password parameter, if you want the AP (Access Point) to be open
            String ssid_with_mac = String(ssid) + String(_device_configs->mac_address);
            String password_with_mac = String(_device_configs->mac_address);
            WiFi.softAP(ssid_with_mac.c_str(), password_with_mac.c_str());
            IPAddress IP = WiFi.softAPIP();
            _device_configs->status.wifi_status = wifi_status_ap_mode;
            logger.log("AP IP address: ");
            logger.log(IP);
        }

        if(_device_configs->status.wifi_status == wifi_status_connected){

            /* check for internet connectivity */
            check_internet(_device_configs);
            if(_device_configs->status.internet_status == internet_failed_at_runtime){
                _device_configs->status.internet_status = internet_failed_at_bigining;
            }
        }

        strcpy(_device_configs->ip_address, WiFi.localIP().toString().c_str());

    }while(false);

    return ret;

}

void wifi_process(){

    // check and fix if there is any issues....

}