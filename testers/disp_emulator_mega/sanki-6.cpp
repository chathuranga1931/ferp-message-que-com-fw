
#include "sanki-6.h"
#include "board_mega.h"
#include "Arduino.h"

uint8_t byte_array[18] = {0xF0, 0x20, 0xFF, 0xFE, 0x0D, 0x4C, 0x8B, 0x5A, 0xF9, 0x18, 0x37, 0x86, 0x75, 0x14, 0x83, 0x62, 0x01, 0x00};
uint8_t byte_array_index = 0;

char byte_array_d2[2] = {0x00, 0x0D};
uint8_t byte_array_d2_index = 0;

const unsigned char clock_low_delay_1_2 = 2; //1st half // 1 out of 2
const unsigned char clock_low_delay_2_2 = 5; // 2nd half // 2 out of 2
const unsigned char clock_hi_delay = 5;

static bool is_nozzel_up = false;
static uint32_t volume_mil = 0;
static uint32_t unit_price = 42300;

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

void create_buffer_sanki_6(uint32_t mili_volume, uint32_t unit_pricex100, bool start_stop){
    
    uint32_t total = (uint32_t)(mili_volume/1000.0 * unit_pricex100 * 1.0);

// #if defined(SHOW_SANKI_BUFFER_GEN_DEGUB)
    Serial.println("VOL: " + String(mili_volume) + "\t UNIT: " + String(unit_pricex100) + "\t Total : " + String(total));
// #endif
    
    uint8_t total_in_array[8];
    uint8_t unit_price_in_array[6];
    uint8_t volume_in_array[8];
    
    for(int i=0; i<7; i++){
        total_in_array[i] = 0xFF;
        volume_in_array[i] = 0xFF;
    }
    for(int i=0; i<5; i++){
        unit_price_in_array[i] = 0xFF;
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

#if defined(SHOW_SANKI_BUFFER_GEN_DEGUB)
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

    byte_array[0] = 0xF0;    
    byte_array[1] = ((unit_price_in_array[1] << 4) | (0 & 0x0F)); 
    
    idx = 2;
    byte_array[idx] = ((volume_in_array[2] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((volume_in_array[3] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((volume_in_array[4] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((volume_in_array[5] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((volume_in_array[6] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((volume_in_array[7] << 4) | ((17-idx) & 0x0F)); idx++;

    idx = 8;
    byte_array[idx] = ((total_in_array[2] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((total_in_array[3] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((total_in_array[4] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((total_in_array[5] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((total_in_array[6] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((total_in_array[7] << 4) | ((17-idx) & 0x0F)); idx++;

    idx = 14;
    byte_array[idx] = ((unit_price_in_array[2] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((unit_price_in_array[3] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((unit_price_in_array[4] << 4) | ((17-idx) & 0x0F)); idx++;
    byte_array[idx] = ((unit_price_in_array[5] << 4) | ((17-idx) & 0x0F)); idx++;   

#if defined(SHOW_SANKI_BUFFER_GEN_DEGUB)
    for (int i = 0; i < 18; i++) {
        if(byte_array[i] <= 0xF){
        Serial.print("0");
      }
      Serial.print(byte_array[i], HEX);
      Serial.print(" ");
    }
    Serial.print("\n");
#endif
}


void sanki_init(){
  create_buffer_sanki_6(volume_mil, unit_price, is_nozzel_up);
}

void sanki_run(){

    bool nozzle_state = digitalRead(c_pin_btn_nzl_status) == LOW ? true : false;
    bool pumping_state = digitalRead(c_pin_btn_pumping) == LOW ? true : false;

    is_nozzel_up = nozzle_state;

    digitalWrite(c_pin_nozzle1_io_port, nozzle_state);
    digitalWrite(c_pin_nozzle2_io_port, nozzle_state);

    if(!nozzle_state){
        // volume_mil = 0;
        // create_buffer_sanki_6(volume_mil, unit_price, is_nozzel_up);
    }

    static bool nozzle_state_prev = false;
    if(nozzle_state_prev != nozzle_state){      
      if(nozzle_state){
        volume_mil = 0;
        create_buffer_sanki_6(volume_mil, unit_price, is_nozzel_up);
        Serial.println("Set to Zero");
      }
      nozzle_state_prev = nozzle_state;      
    }

    if(nozzle_state && pumping_state){
        static unsigned long ts_sync = 0;
        if(millis() - ts_sync > 100){
            ts_sync = millis();
            create_buffer_sanki_6(volume_mil, unit_price, is_nozzel_up);
            volume_mil+= 125;
        }
        
        static bool green_led_state = false;
        digitalWrite(c_pin_green_led, green_led_state);
        green_led_state = !green_led_state;
    }

    digitalWrite(c_pin_red_led, nozzle_state);
    if(!pumping_state) digitalWrite(c_pin_green_led, LOW);

    byte_array_index = 0;
    digitalWrite(c_pin_cs, LOW);
    delayMicroseconds(100);

    // sending first byte
    char tmp = byte_array[byte_array_index++];
    volatile char bit;
    for(int i=0; i<8; i++){ 
        CLOCK_1_LOW;
        delayMicroseconds(clock_low_delay_1_2); 
        bit = ((tmp >> (i)) & 0x01);       //this is just to match the number of total bit shifts to i + 7 - i = 7
        bit = ((tmp >> (7-i)) & 0x01); 
        DATA_1_WRITE(bit);
        delayMicroseconds(clock_low_delay_2_2); 
        CLOCK_1_HIGH;
        delayMicroseconds(clock_hi_delay); 
    }

    delayMicroseconds(100-4); 
    
    // sending second byte
    tmp = byte_array[byte_array_index++];
    for(int i=0; i<8; i++){     
        CLOCK_1_LOW;
        delayMicroseconds(clock_low_delay_1_2); 
        bit = ((tmp >> (i)) & 0x01);       //this is just to match the number of total bit shifts to i + 7 - i = 7
        bit = ((tmp >> (7-i)) & 0x01); 
        DATA_1_WRITE(bit);
        delayMicroseconds(clock_low_delay_2_2); 
        CLOCK_1_HIGH;
        delayMicroseconds(clock_hi_delay); 
    }

    delayMicroseconds(100-4); 

    // sendinng 16 bytes
    for(int j=0; j<16; j++){
        tmp = byte_array[byte_array_index++];
        for(int i=0; i<8; i++){     
            CLOCK_1_LOW;
            delayMicroseconds(clock_low_delay_1_2); 
            bit = ((tmp >> (i)) & 0x01);       //this is just to match the number of total bit shifts to i + 7 - i = 7
            bit = ((tmp >> (7-i)) & 0x01); 
            DATA_1_WRITE(bit);
            delayMicroseconds(clock_low_delay_2_2); 
            CLOCK_1_HIGH;
            delayMicroseconds(clock_hi_delay); 
        }

        CS_1_WRITE(1);
        delayMicroseconds(5); 
        CS_1_WRITE(0);

        delayMicroseconds(5); 
    }
}