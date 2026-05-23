
#include "censtar-6.h"
#include "board_mega.h"
#include "Arduino.h"

#define PACKET_SIZE 16
#define SHOW_CENSTAR_BUFFER_GEN_DEGUB

static unsigned char  device_status = 0;
uint8_t channel1_tx_buffer[16] = {};
const unsigned char waiting_for_button_press = 0;
const unsigned char sending = 1;
const unsigned char waiting_for_7ms = 2;
const unsigned char waiting_for_900ms = 3;
const unsigned char verify_data = 4;
static bool is_nozzel_up = false;
bool is_running_button_pressed = false;
static uint32_t volume_mil = 0;
static uint32_t unit_price = 12345;
unsigned int buffer_idx = 0;
unsigned int blink_delay = 500;
unsigned char repeat_count = 0;
unsigned char bite = 0;
unsigned long ts = 0;

static int count_digits(uint32_t num) {
    int count = 0;

    if (num == 0) {
        return 1; // Special case: 0 has one digit
    }

    while (num != 0) {
        count++;
        num /= 10;
    }

    return count;
}


void create_buffer_censtar_6(uint32_t mili_volume, uint32_t unit_pricex100, bool start_stop){
    
    uint32_t total = (uint32_t)(mili_volume/1000.0 * unit_pricex100 * 1.0);

#if defined(SHOW_CENSTAR_BUFFER_GEN_DEGUB)
    Serial.println("VOL: " + String(mili_volume) + "\t UNIT: " + String(unit_pricex100) + "\t Total : " + String(total));
#endif
    
    uint8_t total_in_array[8];
    uint8_t unit_price_in_array[6];
    uint8_t volume_in_array[8];
    
    for(int i=0; i<7; i++){
        total_in_array[i] = 0x00;
        volume_in_array[i] = 0x00;
    }
    for(int i=0; i<5; i++){
        unit_price_in_array[i] = 0x00;
    }

    uint32_t value = total;
    int correction = total/1000 + count_digits(total) < 3 ? 3 : count_digits(total);
    for (int i = 7; i > 7 - correction; i--) {
        total_in_array[i] = value % 10;
        value /= 10;
    }

    value = unit_pricex100;
    correction = unit_pricex100/1000 + count_digits(unit_pricex100) < 3 ? 3 : count_digits(unit_pricex100);
    for (int i = 5; i > 5 - correction; i--) {
        unit_price_in_array[i] = value % 10;
        value /= 10;
    }

    value = mili_volume;
    correction = mili_volume/1000 + count_digits(mili_volume) < 4 ? 4 : count_digits(mili_volume);
    for (int i = 7; i > 7 - correction; i--) {
        volume_in_array[i] = value % 10;
        value /= 10;
    }

#if defined(SHOW_CENSTAR_BUFFER_GEN_DEGUB)
    // Serial.print the resulting array
    for (int i = 0; i < 8; i++) {
        Serial.print(total_in_array[i], HEX);
    }
    Serial.print("\n");
    // Print the resulting array
    for (int i = 0; i < 6; i++) {
        Serial.print(unit_price_in_array[i], HEX);
    }
    Serial.print("\n");
    // Print the resulting array
    for (int i = 0; i < 8; i++) {
        Serial.print(volume_in_array[i], HEX);
    }
    Serial.print("\n");
#endif

    uint32_t idx = 0;
    uint32_t sub_idx = 3;
    channel1_tx_buffer[idx++] = ((unit_price_in_array[4] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((unit_price_in_array[3] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((unit_price_in_array[2] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((unit_price_in_array[1] << 4) | (idx & 0x0F));
    
    channel1_tx_buffer[idx++] = ((total_in_array[6] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[5] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[4] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[3] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[2] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[1] << 4) | (idx & 0x0F));
    
    channel1_tx_buffer[idx++] = ((volume_in_array[7] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((volume_in_array[6] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((volume_in_array[5] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((volume_in_array[4] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((volume_in_array[3] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((volume_in_array[2] << 4) | (idx & 0x0F));

#if defined(SHOW_CENSTAR_BUFFER_GEN_DEGUB)
    for (int i = 0; i < 16; i++) {
      if(channel1_tx_buffer[i] <= 0xF){
        Serial.print("0");
      }
      Serial.print(channel1_tx_buffer[i], HEX);
      Serial.print(" ");
    }
    Serial.print("\n");
#endif
}

void censtar_6dig_init(){

    digitalWrite(c_pin_sdata1, LOW); 
    digitalWrite(c_pin_sdata2, LOW);
    digitalWrite(c_pin_sclk, LOW); 
    digitalWrite(c_pin_rclk, LOW); 

    create_buffer_censtar_6(volume_mil, unit_price, is_nozzel_up);
}


void send_byte_censtar_6(unsigned char bite1)
{
    for (int i = 0; i < 8; i++)
    {
        unsigned char v1 = (bite1 & 0b10000000);
        bite1 = bite1 << 1;
        v1 = v1 >> 7;
        digitalWrite(c_pin_sdata1, v1);
        delayMicroseconds(1);
        digitalWrite(c_pin_sclk, LOW);
        delayMicroseconds(15);
        digitalWrite(c_pin_sclk, HIGH);
        if (i == 7) //at the last bit, toggle rclk
        {
            delayMicroseconds(9);
            digitalWrite(c_pin_rclk, HIGH);
            delayMicroseconds(14);
            digitalWrite(c_pin_rclk, LOW);
            delayMicroseconds(48);
        }
        else
        {
            delayMicroseconds(36);
        }
    }
}


void censtar_6dig_run(){  

    bool nozzle_state = digitalRead(c_pin_btn_nzl_status) == LOW ? true : false;
    bool pumping_state = digitalRead(c_pin_btn_pumping) == LOW ? true : false;

    is_nozzel_up = nozzle_state;

    digitalWrite(c_pin_nozzle1_io_port, nozzle_state);
    digitalWrite(c_pin_nozzle2_io_port, nozzle_state);

    if(!nozzle_state){
        // volume_mil = 0;
        // create_buffer_censtar_6(volume_mil, unit_price, is_nozzel_up);
    }  

    static bool nozzle_state_prev = false;
    if(nozzle_state_prev != nozzle_state){      
      if(nozzle_state){
        volume_mil = 0;
        create_buffer_censtar_6(volume_mil, unit_price, is_nozzel_up);
        Serial.println("Set to Zero");
      }
      nozzle_state_prev = nozzle_state;      
    }

    if(nozzle_state && pumping_state){
        static unsigned long ts_sync = 0;
        if(millis() - ts_sync > 100){
            ts_sync = millis();
            create_buffer_censtar_6(volume_mil, unit_price, is_nozzel_up);
            volume_mil+= 49;
        }
        
        static bool green_led_state = false;
        digitalWrite(c_pin_green_led, green_led_state);
        green_led_state = !green_led_state;
    }

    digitalWrite(c_pin_red_led, nozzle_state);
    if(!pumping_state) digitalWrite(c_pin_green_led, LOW);

    static unsigned long ts_sending = 0;
    if(millis() - ts_sending > 50){

        for(bite = 0; bite<PACKET_SIZE; bite++){
            send_byte_censtar_6(channel1_tx_buffer[buffer_idx + bite]);
        }
        ts_sending = millis();
    } 
}