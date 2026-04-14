

#include <Arduino.h>
#include <ESP32Ping.h>

#include "logger.h"
#include "error.h"
#include "device_config.h"

void check_internet(device_configs_t * device_configs){

    do{

        bool success = Ping.ping("www.google.com", 3);
        if(!success){
            device_configs->status.internet_status = internet_failed_at_runtime;
            logger.log("Ping failed");
            return;
        }

        device_configs->status.internet_status = internet_connected;
        logger.log("Ping succesful.");

    }while(false);
}