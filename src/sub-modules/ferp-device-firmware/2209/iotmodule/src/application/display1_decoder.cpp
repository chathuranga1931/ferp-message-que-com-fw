
#include <Arduino.h>    
#include <WebSerial.h>

#include "device_config.h"
#include "logger.h"
#include "error.h"
#include "nozzel_event.h"

#include "que.h"

static unsigned char buffer_a[15] = {0};
static unsigned char buffer_b[15] = {0};

enum decoder_state_t{
    wait_for_s = 0,
    wait_for_t,
    wait_for_x,
    wait_for_protocol_version,
    wait_for_input_sig_complete,
    wait_for_data_complete,
};

enum recieve_buffer_t{
    recieve_buffer_a,
    recieve_buffer_b,
};

static decoder_state_t decoder_state;
static decoder_state_t decoder_state_prev;
static int state_change_reset_counter = 0;
static unsigned int protocol_version = 0;
static unsigned char input_sig = 0;

static device_configs_t * _device_configs =  nullptr;

typedef struct packet_t_{
    unsigned char buffera[14];
    unsigned char bufferb[14];
    unsigned char input_sig;
}packet_t;

#define DECODE_PACKET_QUE_SIZE  (5)

packet_t packet_que_buffer[DECODE_PACKET_QUE_SIZE];
que_t packet_que = {0};
que_t * _n_event_que = 0;
que_t * _printing_event_que = 0;

void copy_packet(void * dest, void * src){
    packet_t * src_packet = (packet_t *) src;
    packet_t * dest_packet = (packet_t *) dest;


    // logger.log("Copping buffer(Input) = ");
    // logger.log_buffer(src_packet->buffera, sizeof(src_packet->buffera));
    // logger.log_buffer(src_packet->buffera, sizeof(src_packet->buffera));

    dest_packet->input_sig = src_packet->input_sig;
    memcpy(dest_packet->buffera, src_packet->buffera, sizeof(src_packet->buffera));
    memcpy(dest_packet->bufferb, src_packet->bufferb, sizeof(src_packet->bufferb));
}

void display_decoder_init(device_configs_t * device_configs, que_t * n_event_que, que_t * printing_even_que){

    ret_t ret = ret_Success;

    do{
        if(device_configs == nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("TS: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        _device_configs = device_configs;

        if(n_event_que == nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("TS: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        _n_event_que = n_event_que;

        if(printing_even_que == nullptr){
            ret = ret_Err_Gen_NullP;
            logger.log("TS: [" + String(__FILENAME__) + "]" + String(__LINE__) + " Err:" + String(ret));
            break;
        }

        _printing_event_que = printing_even_que;

        packet_que.size_of_type = sizeof(packet_t);
        packet_que.copy = copy_packet;
        packet_que.buffer_size = DECODE_PACKET_QUE_SIZE;
        packet_que.buffer = packet_que_buffer;

    }while(false);
}

void on_data_received(unsigned char b){

    state_change_reset_counter++;
    if(decoder_state_prev != decoder_state){
        state_change_reset_counter=0;
        decoder_state_prev = decoder_state;
        // logger.log("State Changed " + String(decoder_state));
    }

    switch(decoder_state){

        case wait_for_s:
            if(b=='s'){
                decoder_state = wait_for_t;
            }
        break;

        case wait_for_t:
            if(b=='t'){
                decoder_state = wait_for_x;
            }
            else{
                decoder_state = wait_for_s;
            }
        break;

        case wait_for_x:
            if(b=='x'){
                decoder_state = wait_for_protocol_version;
            }
            else{
                decoder_state = wait_for_s;
            }
        break;

        case wait_for_protocol_version:
            if(state_change_reset_counter == 1){
                protocol_version = protocol_version << 8;
                protocol_version |= b - '0';
                decoder_state = wait_for_input_sig_complete;
            }
            else{
                protocol_version = b - '0';
            }
        break;

        case wait_for_input_sig_complete:
            input_sig = b;
            decoder_state = wait_for_data_complete;
        break;

        case wait_for_data_complete:

            if( state_change_reset_counter%2 == 0){
                buffer_a[state_change_reset_counter/2] = b;
            }
            else{
                buffer_b[(state_change_reset_counter-1)/2] = b;
            }

            if(state_change_reset_counter == 27){
                packet_t tmp;
                tmp.input_sig = input_sig;

                // logger.log("Input buffer (LSB) = ");
                // logger.log_buffer(buffer_a_int, sizeof(buffer_a_int));
                // logger.log_buffer(buffer_b_int, sizeof(buffer_b_int));

                // for(int i=0; i<15; i++){
                //     char tmp_var = buffer_a_int[i];
                //     char tmp_var_msb = 0;
                //     for(int j=0; j<8; j++){
                //     tmp_var_msb = tmp_var_msb << 1;
                //     tmp_var_msb |= tmp_var & 0x01;
                //     tmp_var = tmp_var >> 1;
                //     }
                //     buffer_a_int[i] = tmp_var_msb;
                // }
                // for(int i=0; i<15; i++){
                //     char tmp_var = buffer_b_int[i];
                //     char tmp_var_msb = 0;
                //     for(int j=0; j<8; j++){
                //     tmp_var_msb = tmp_var_msb << 1;
                //     tmp_var_msb |= tmp_var & 0x01;
                //     tmp_var = tmp_var >> 1;
                //     }
                //     buffer_b_int[i] = tmp_var_msb;
                // }

                // logger.log("Input buffer (MSB) = ");
                // logger.log_buffer(buffer_a_int, sizeof(buffer_a_int));
                // logger.log_buffer(buffer_b_int, sizeof(buffer_b_int));

                // for(int i=0; i<15; i++){
                //     char tmp_var1 = buffer_a_int[i];
                //     char tmp_var2 = buffer_b_int[i+1];
                //     buffer_a[(tmp_var1 & 0x0F)] = tmp_var1;
                //     buffer_b[(tmp_var2 & 0x0F)] = tmp_var2;
                // }

                // logger.log("Input buffer*(MSB) = ");
                // logger.log_buffer(buffer_a, sizeof(buffer_a));
                // logger.log_buffer(buffer_a, sizeof(buffer_b));

                memcpy(tmp.buffera, buffer_a, sizeof(tmp.buffera));
                memcpy(tmp.bufferb ,buffer_b, sizeof(tmp.bufferb));
                que_push(&packet_que, (void *)(&tmp));
                decoder_state = wait_for_s;
            }
        break;

        default:
        break;
    }
}

char get_char(unsigned char value){

    char character = 0;

    value = value >> 4;

    switch(value){
        case 10:
            character = 'L';
        break;
        case 11:
            character = 'H';
        break;
        case 12:
            character = 'P';
        break;
        case 13:
            character = 'R';
        break;
        case 14:
            character = '-';
        break;
        case 15:
            character = ' ';
        break;
        default:
            character = '0' + value;
        break;
    }

    return character;
}

bool is_buffer_valid(unsigned char * buffer){

    return true;
    for(int i=0; i<14; i++){
        if((buffer[i] & 0x0F) != i){
            return false;
        }
    }

    return true;
}

/*
0	1	2	3	4	5	6	7	8	9	10	11	12	13
	Total Price								Unit Price
L	 	 	4	5	0	0	0	 	4	5	0	0	0
P	 	 	1	0	0	0	L	L	1	L	 	L
	Liters

*/

void get_display_data(display_data_t * dd, uint8_t * ba, uint8_t * bb){

    for(int i=0; i<14; i++){
        if((ba[i] >> 4) >=10) ba[i] = 0;
        if((bb[i] >> 4) >=10) bb[i] = 0;
    }

    dd->unit_price =    (ba[8] >>  4)  * 1000.0 +
                        (ba[9] >>  4)  * 100.0 +
                        (ba[10] >> 4)  * 10.0 +
                        (ba[11] >> 4)  * 1.0 +
                        (ba[12] >> 4)  * 0.1 +
                        (ba[13] >> 4)  * 0.01 ;


    dd->total_price = (ba[1] >>  4)  * 10000.0 +
                      (ba[2] >>  4)  * 1000.0 +
                      (ba[3] >>  4)  * 100.0 +
                      (ba[4] >>  4)  * 10.0 +
                      (ba[5] >>  4)  * 1.0 +
                      (ba[6] >>  4)  * 0.1 +
                      (ba[7] >>  4)  * 0.01;

    dd->volume_l = (bb[1] >>  4)  * 1000.0 +
                   (bb[2] >>  4)  * 100.0 +
                   (bb[3] >>  4)  * 10.0 +
                   (bb[4] >>  4)  * 1.0 +
                   (bb[5] >>  4)  * 0.1 +
                   (bb[6] >>  4)  * 0.01 +
                   (bb[7] >>  4)  * 0.001;

    if((bb[8] >> 4) == 1){
        dd->start_stop = true;
    }
    else{
        dd->start_stop = false;
    }

    if((ba[0] >> 4) == 10){
        dd->select_l = true;
    }
    else{
        dd->select_l = false;
    }

    if((bb[0] >> 4) == 12){
        dd->select_p = true;
    }
    else{
        dd->select_p = false;
    }
}

bool is_button_pressed(unsigned char input_sig){

    static unsigned char prev_input_sig = 0;
    if(prev_input_sig != input_sig){
        prev_input_sig = input_sig;

        if(input_sig & 0x08){
            return true;
        }
    }

    return false;
}

nozzel_event_t last_valid_ne = {0};
void display_decoder_process(){

    int quesize = 0;
    quesize = que_getsize(&packet_que);
    if(quesize){
        packet_t tmp;
        que_pop(&packet_que, (void *)(&tmp));

        if(is_button_pressed(tmp.input_sig)){
            logger.log("Que printing events...");
            que_push(_printing_event_que, &last_valid_ne);
        }

        // logger.log("Popped buffer = ");
        // logger.log_buffer(tmp.buffera, sizeof(tmp.buffera));
        // logger.log_buffer(tmp.bufferb, sizeof(tmp.bufferb));
        // logger.log("Popped Input Signal = " + String(tmp.input_sig));

        /* Validate buffers */
        if(is_buffer_valid(tmp.buffera) && is_buffer_valid(tmp.bufferb)){

            display_data_t display_data;
            get_display_data(&display_data, tmp.bufferb, tmp.buffera);
            
            double total_price_expected = display_data.unit_price*display_data.volume_l;
            double gap = total_price_expected - display_data.total_price;
            double modulo_gap = gap < 0 ? gap*(-1) : gap;
            if(modulo_gap > 10){
                logger.log("Total mismatch detected....");
            }

            // String r1 = "";
            // String r2 = "";
            // for(int i=0; i<14; i++){
            //     r1 += get_char(tmp.buffera[i]);
            //     r2 += get_char(tmp.bufferb[i]);
            // }

            // logger.log_(r1 + "   ");
            // logger.log_(r2 + "   ");
            // logger.log_(String(tmp.input_sig, 2));
            // logger.log_("\r\n");

            String start_stop = display_data.start_stop ? "Start" : "Stop";
            String p_or_l = display_data.select_l ? "L" : display_data.select_p ? "P" : " ";
            static double total_prev = 0;
            static bool start_stop_prev = false;
            //if(total_prev != display_data.total_price || start_stop_prev != display_data.start_stop){
                total_prev = display_data.total_price;
                start_stop_prev = display_data.start_stop;
                logger.log_( "Decoded data:  " + String(display_data.unit_price) +
                                                " " + String(display_data.volume_l, 3) +
                                                " " + String(display_data.total_price) +
                                                " " + start_stop +
                                                " " + p_or_l + "\r\n");
                // WebSerial.println("Decoded data:  " + String(display_data.unit_price) +
                //                 " " + String(display_data.volume_l, 3) +
                //                 " " + String(display_data.total_price) +
                //                 " " + start_stop +
                //                 " " + p_or_l + "\r\n");

                _device_configs->display_value_last.select_l = display_data.select_l;
                _device_configs->display_value_last.select_p = display_data.select_p;
                _device_configs->display_value_last.total_price = display_data.total_price;
                _device_configs->display_value_last.unit_price = display_data.unit_price;
                _device_configs->display_value_last.volume_l = display_data.volume_l;
                _device_configs->display_value_last.start_stop = display_data.start_stop;

                _device_configs->display_value_last_str = String(display_data.unit_price) +
                                " " + String(display_data.volume_l, 3) +
                                " " + String(display_data.total_price) +
                                " " + start_stop +
                                " " + p_or_l;
            //}

            static bool prev_start_stop = false;
            if(display_data.start_stop == false && prev_start_stop == true){
                logger.log_( "Decoded data: (Final)" + String(display_data.unit_price) +
                                " " + String(display_data.volume_l, 3) +
                                " " + String(display_data.total_price) +
                                " " + start_stop +
                                " " + p_or_l + "\r\n");
                WebSerial.println("Decoded data (Final):  " + String(display_data.unit_price) +
                                " " + String(display_data.volume_l, 3) +
                                " " + String(display_data.total_price) +
                                " " + start_stop +
                                " " + p_or_l + "\r\n");
                struct timeval now;
                gettimeofday(&now, NULL);
                last_valid_ne.time_stamp = now.tv_sec;
                last_valid_ne.total_price = display_data.total_price;
                last_valid_ne.unit_price = display_data.unit_price;
                last_valid_ne.volume_l = display_data.volume_l;
                que_push(_n_event_que, &last_valid_ne);
            }
            prev_start_stop = display_data.start_stop;
        }
        else{
            logger.log("Invalid data...");
        }
    }
}