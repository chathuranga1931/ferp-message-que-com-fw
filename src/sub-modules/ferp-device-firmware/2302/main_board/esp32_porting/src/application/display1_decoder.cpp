
#include <Arduino.h>    
#include <WebSerial.h>

#include "device_config.h"
#include "logger.h"
#include "error.h"
#include "nozzel_event.h"

#include "que.h"
#include "sd_logger.h"

static device_configs_t * _device_configs =  nullptr;

void display_decoder_init(device_configs_t * device_configs){

    ret_t ret = ret_Success;

    do{
        if(device_configs == nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("TS: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        _device_configs = device_configs;

    }while(false);
}

nozzel_event_t last_valid_ne = {0};
void display_decoder_process(display_data_t display_data, uint8_t nozzel_id, que_t * n_event_que, bool * prev_start_stop){

    ret_t ret = ret_Success;
    do{
        ret = ret_Err_Gen_NullP;
        if(_device_configs == nullptr){
            logger.log("TS: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        // logger.log("Received, NID=" + String(nozzel_id) + 
        //     " Data = " + String(display_data.total_price) + 
        //     "," + String(display_data.unit_price) + 
        //     "," + String(display_data.volume_l) + 
        //     "," + String(display_data.start_stop) + 
        //     "");

        double total_price_expected = display_data.unit_price*display_data.volume_l;
        double gap = total_price_expected - display_data.total_price;
        double modulo_gap = gap < 0 ? gap*(-1) : gap;
        if(modulo_gap > 10){
            // write_error("FERP-Display: Total mismatch detected" );
            // write_debug_log("Total mismatch detected.... ignored");
            logger.log("Total mismatch detected.... Expected = " + String(total_price_expected) + " Actual = " + 
                String(display_data.total_price) + ": ignored");
            break;
        }

        String start_stop = display_data.start_stop ? "Start" : "Stop";
        String p_or_l = display_data.select_l ? "L" : display_data.select_p ? "P" : " ";

        _device_configs->nozzel_data[nozzel_id].display_value_last.select_l = display_data.select_l;
        _device_configs->nozzel_data[nozzel_id].display_value_last.select_p = display_data.select_p;
        _device_configs->nozzel_data[nozzel_id].display_value_last.total_price = display_data.total_price;
        _device_configs->nozzel_data[nozzel_id].display_value_last.unit_price = display_data.unit_price;
        _device_configs->nozzel_data[nozzel_id].display_value_last.volume_l = display_data.volume_l;
        _device_configs->nozzel_data[nozzel_id].display_value_last.start_stop = display_data.start_stop;

        _device_configs->nozzel_data[nozzel_id].display_value_last_str = String(display_data.unit_price) +
                        " " + String(display_data.volume_l, 3) +
                        " " + String(display_data.total_price) +
                        " " + start_stop +
                        " " + p_or_l;

        if(display_data.volume_l == 0){
            logger.log("0 Volume, ignored for storing...");
            // write_debug_log("0 Volume, ignored for storing...");
            break;
        }

        // logger.log("Display IDX = " + String(nozzel_id));
        if(display_data.start_stop == false && *prev_start_stop == true){
            logger.log_( "Decoded data: (Final) NID=" + String(nozzel_id) +
                            " " + String(display_data.unit_price) +
                            " " + String(display_data.volume_l, 3) +
                            " " + String(display_data.total_price) +
                            " " + start_stop +
                            " " + p_or_l + "\r\n");
            WebSerial.println("Decoded data: (Final) NID=" + String(nozzel_id) +
                            " " + String(display_data.unit_price) +
                            " " + String(display_data.volume_l, 3) +
                            " " + String(display_data.total_price) +
                            " " + start_stop +
                            " " + p_or_l + "\r\n");
            // write_note("FERP-Display: Date decoded Success" );
            // write_debug_log("NID=" + String(nozzel_id) +
            //                 " " + String(display_data.unit_price) +
            //                 " " + String(display_data.volume_l, 3) +
            //                 " " + String(display_data.total_price) +
            //                 " " + start_stop +
            //                 " " + p_or_l);
            write_note(String(display_data.unit_price) +
                            " " + String(display_data.volume_l, 3) +
                            " " + String(display_data.total_price) +
                            " " + start_stop +
                            " " + p_or_l);
            struct timeval now;
            gettimeofday(&now, NULL);
            last_valid_ne.time_stamp = now.tv_sec;
            last_valid_ne.total_price = display_data.total_price;
            last_valid_ne.unit_price = display_data.unit_price;
            last_valid_ne.volume_l = display_data.volume_l;
            que_push(n_event_que, &last_valid_ne);
            _device_configs->nozzel_stat[nozzel_id].nozzel_event_count++;
        }

        *prev_start_stop = display_data.start_stop;

    }while(false);
}