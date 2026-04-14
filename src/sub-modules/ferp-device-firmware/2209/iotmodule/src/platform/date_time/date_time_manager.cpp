
#include <Arduino.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <Wire.h>

#ifdef FERP_COM
#include <ErriezDS1307.h>
#endif

#include "device_config.h"
#include "logger.h"
#include "error.h"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
String formattedDate;
#ifdef FERP_COM
ErriezDS1307 rtc;
#endif
String dayStamp;
String timeStamp;

#define DATE_STRING_SHORT           3

// Month names in flash
const char monthNames_P[] PROGMEM = "JanFebMarAprMayJunJulAugSepOctNovDec";
// Day of the week names in flash
const char dayNames_P[] PROGMEM= "SunMonTueWedThuFriSat";
static device_configs_t * _device_configs =  nullptr;

static ret_t update_time_from_ntp(time_t * ephoc_time){

    ret_t ret = ret_Success;

    do{
        timeClient.begin();
        // Set offset time in seconds to adjust for your timezone, for example:
        // GMT +1 = 3600
        // GMT +8 = 28800
        // GMT -1 = -3600
        // GMT 0 = 0
        timeClient.setTimeOffset(_device_configs->datetime.region * 3600.0);

        unsigned long ts = millis();
        _device_configs->status.date_time_status = date_time_updated;
        while(!timeClient.update()) {
            if(millis() - ts > (_device_configs->datetime.wait_time_s * 1000)){
                _device_configs->status.date_time_status = date_time_pending;
                break;
            }

            if(timeClient.forceUpdate()){
                break;
            }
            delay(500);
        }

        if(_device_configs->status.date_time_status == date_time_pending){
            ret = ret_Err_App_TimeOut;
            logger.log("TS: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        // The formattedDate comes with the following format:
        // 2018-05-28T16:00:13Z
        // We need to extract date and time
        formattedDate = timeClient.getFormattedDate();
        logger.log(formattedDate);

        // Extract date
        int splitT = formattedDate.indexOf("T");
        dayStamp = formattedDate.substring(0, splitT);
        logger.log("DATE: ");
        logger.log(dayStamp);
        // Extract time
        timeStamp = formattedDate.substring(splitT+1, formattedDate.length()-1);
        logger.log("HOUR: ");
        logger.log(timeStamp);

        *ephoc_time = timeClient.getEpochTime();

    }while(false);

    return ret;
}

String get_formatted_time(long tv_sec){
    // struct timeval now;
    // gettimeofday(&now, NULL);
    return timeClient.getFormattedDate(tv_sec, _device_configs->datetime.region_str);
}

ret_t get_time(struct tm * timeinfo){

    // if(!getLocalTime(timeinfo)){
    //     return ret_Err_Hdware_GetLocalTime;
    // }

    // logger.log("TS: Formatted Time : " + get_formatted_time());
    return ret_Success;
}

time_t get_epoch_time(){
    logger.log("TS: Warning!!! Not implemented");
    return 1234;
}

static ret_t update_time_from_rtc(){

    ret_t ret = ret_Success;    
#ifdef FERP_COM
    do{
        //load time from rtc
        // Read date/time
        time_t time_rtc = rtc.getEpoch();
        if (time_rtc == 0) {
            logger.log(F("TS: Read date/time from RTC failed"));
            logger.log(F("TS: Err!! No source to sysn time available..."));
        }
        else{
            //https://www.esp32.com/viewtopic.php?t=27513
            // tm.tm_year = (Runtime.year + 2000) - 1900;
            // tm.tm_mon = Runtime.month - 1;
            // tm.tm_mday = Runtime.day;
            // tm.tm_hour = Runtime.hour;
            // tm.tm_min = Runtime.minute;
            // tm.tm_sec = Runtime.second;
            // time_t t = mktime(&tm);
            // ESP_LOGI(TAG, "Setting System Time: %s", asctime(&tm));
            logger.log(F("TS: Updated system time from RTC"));
            struct timeval now = { .tv_sec = time_rtc };
            settimeofday(&now, NULL);
        }

    }while(false);
#endif
    return ret;
}

ret_t date_time_init(device_configs_t * device_configs){

    ret_t ret = ret_Success;

    do{
        if(device_configs == nullptr){
			ret = ret_Err_Gen_NullP;
            logger.log("TS: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
			break;
        }

        _device_configs = device_configs;

        // Initialize RTC

#ifdef FERP_COM
        int retry_count = 3;
        _device_configs->status.rtc_status = rtc_not_available;
        do{
            if(rtc.begin()){
                _device_configs->status.rtc_status = rtc_available;
                logger.log("TS: RTC Detected");
                break;
            }

            if(retry_count == 0){
                logger.log("TS: RTC failed to detect");
                break;
            }

            retry_count --;
            delay(500);

        } while(true);
#endif

        /* if WiFi available */
        if(_device_configs->status.wifi_status == wifi_status_connected){

            if(_device_configs->status.internet_status == internet_connected){

                // try load date time
                time_t ephoc_time;
                ret = update_time_from_ntp(&ephoc_time);

                // if failed to load
                if(ret != ret_Success){
                    logger.log(F("TS: Read date/time from NTP Failed"));
                    // check rtc
                    #ifdef FERP_COM
                    if(_device_configs->status.rtc_status == rtc_available){

                        ret = update_time_from_rtc();
                    }
                    #endif
                }
                // else ntp available
                else{

                    logger.log(F("TS: Updated system time from NTP"));
                    struct timeval now = { .tv_sec = ephoc_time };
                    settimeofday(&now, NULL);
                    #ifdef FERP_COM
                    // check rtc
                    if(_device_configs->status.rtc_status == rtc_available){
                        // update rtc
                        logger.log(F("TS: Updated RTC time from NTP"));
                        rtc.setEpoch(ephoc_time);
                    }
                    #endif
                }
            }
            // no internet
            else{
                logger.log(F("TS: No Internet"));
                // check rtc
                #ifdef FERP_COM
                if(_device_configs->status.rtc_status == rtc_available){

                    ret = update_time_from_rtc();
                }
                #endif
            }
        }
        /* if WiFi Not available */
        else{

            logger.log(F("TS: No WiFi"));
            // check rtc
            #ifdef FERP_COM
            if(_device_configs->status.rtc_status == rtc_available){
                //load time from rtc
                ret = update_time_from_rtc();
            }
            else{
                // use default time
                logger.log(F("TS: Err!! No source to sysn time available..."));
            }
            #endif
        }

    }while(false);

    return ret;
}