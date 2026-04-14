#include <Arduino.h>

#define PACKET_SIZE 16

const char c_pin_sdata1=3;
const char c_pin_sdata2=2;
const char c_pin_sclk=6;
const char c_pin_rclk=4;

const char c_pin_rclk_other=7; /*This pin is not used*/
const char c_pin_5v=5;

const char c_pin_serial_tx=A4;
const char c_pin_serial_rx=A5;
const char c_pin_button= A0;
const char c_pin_button_Tester= A1;

const char c_pin_red_led=13;
const char c_pin_green_led=12;

// const char c_pin_sdata1 = 3;
// const char c_pin_sclk = 5;
// const char c_pin_rclk = 6;

// const char c_pin_red_led = 10;
// const char c_pin_green_led = 9;
// const char c_pin_button = 13; // send start signal

const size_t repeat_packet = 100;

const unsigned char sdata1_tx_buffer[][PACKET_SIZE] = {
    {0x00, 0x01, 0x62, 0x33, 0x04, 0x05, 0x06, 0x67, 0x58, 0x79, 0x0A, 0x0B, 0x0C, 0x0D, 0x1E, 0x2F},
    {0x00, 0x01, 0x62, 0x33, 0x04, 0x05, 0x06, 0x67, 0x58, 0x79, 0x0A, 0x0B, 0x0C, 0x0D, 0x1E, 0x2F},
    {0x00, 0x01, 0x62, 0x33, 0x04, 0x05, 0x06, 0x67, 0x58, 0x79, 0x0A, 0x0B, 0x0C, 0x0D, 0x1E, 0x2F},
    {0x00, 0x01, 0x62, 0x33, 0x04, 0x05, 0x06, 0x67, 0x58, 0x79, 0x0A, 0x0B, 0x0C, 0x0D, 0x1E, 0x2F}};


uint8_t channel1_tx_buffer[16] = {};

void send_byte(unsigned char bite1);

void nop()
{
    static volatile int i;
    i++;
}

int count_digits(uint32_t num) {
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

    Serial.println("VOL: " + String(mili_volume) + "\t UNIT: " + String(unit_pricex100) + "\t Total : " + String(total));
    
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

    for (int i = 0; i < 16; i++) {
      if(channel1_tx_buffer[i] <= 0xF){
        Serial.print("0");
      }
      Serial.print(channel1_tx_buffer[i], HEX);
      Serial.print(" ");
    }
    Serial.print("\n");
}

void setup()
{
    pinMode(c_pin_sdata1, OUTPUT);
    pinMode(c_pin_sdata2, OUTPUT);
    pinMode(c_pin_sclk, OUTPUT);
    pinMode(c_pin_rclk, OUTPUT);
    pinMode(c_pin_red_led, OUTPUT);
    pinMode(c_pin_green_led, OUTPUT);
    pinMode(c_pin_button, INPUT_PULLUP);
    pinMode(c_pin_button_Tester, INPUT_PULLUP);
    pinMode(c_pin_serial_rx, INPUT);
    pinMode(c_pin_serial_tx, OUTPUT);
    pinMode(c_pin_5v, OUTPUT);
    digitalWrite(c_pin_5v, HIGH);


    digitalWrite(c_pin_sdata1, LOW); 
    digitalWrite(c_pin_sdata2, LOW);
    digitalWrite(c_pin_sclk, LOW); 
    digitalWrite(c_pin_rclk, LOW);  

    Serial.begin(115200);  

    Serial.println("Packet Size = " + String(PACKET_SIZE) + " Row Size = " + String(sizeof(sdata1_tx_buffer) / PACKET_SIZE));
}


const unsigned char waiting_for_button_press = 0;
const unsigned char sending = 1;
const unsigned char waiting_for_7ms = 2;
const unsigned char waiting_for_900ms = 3;
const unsigned char verify_data = 4;

static unsigned char  device_status = 0;
static unsigned char  device_status_prev = 0xFF;
unsigned long ts = 0;
unsigned char bite = 0;

unsigned int buffer_idx = 0;

unsigned int verify_buffer_idx = 0;
unsigned char verify_buffer[100] = {0};
unsigned char verify_buffer_inter[100] = {0};
unsigned char decoder_state = 0;

unsigned int last_sent_buffer_idx = 0;
unsigned int blink_delay = 500;

unsigned char repeat_count = 0;
unsigned char input_sig_prev = 0;

bool auto_start_enabled = true;
#define AUTO_START_DELAY_MSEC        (4000)
unsigned long auto_start_last_time_stamp; 

typedef struct {
    uint32_t milli_vol;
    uint32_t unit_pricex100;
} value_pair_t;

const uint32_t value_table_size = 15;
value_pair_t value_table[value_table_size] = {
    {0,45000},
    {0,45000},
    {0,45000},
    {111,45000},
    {222,45000},
    {333,45000},
    {444,45000},
    {555,45000},
    {666,45000},
    {777,45000},
    {777,45000},
    {777,45000},
    {777,45000},
    {777,45000},
    {777,45000},
};

uint32_t value_tbl_idx = 0;

uint32_t volume_mil = 270000;
uint32_t unit_price = 42300;
bool is_running_button_pressed = false;
bool is_nozzel_up = false;

void loop() {

    switch(device_status){
        case waiting_for_button_press:
            if(is_nozzel_up){
              if(digitalRead(c_pin_button) == HIGH){
                  delay(50);
                  create_buffer_censtar_6(volume_mil, unit_price, is_nozzel_up);
                  volume_mil+= 500;
                  buffer_idx = 0;
                  blink_delay = 175;
                  digitalWrite(c_pin_red_led, HIGH);
                  is_running_button_pressed = true;
              }
              else{
                digitalWrite(c_pin_green_led, LOW);
                is_running_button_pressed = false;
              }
            }else{

            }

            if(digitalRead(c_pin_button_Tester) == LOW){

                if(is_nozzel_up) is_nozzel_up = false;
                else is_nozzel_up = true;

                Serial.println("Button Pressed...");
                delay(50);
                while(digitalRead(c_pin_button_Tester) == LOW);
                Serial.println("Button Released...");

                if(is_nozzel_up){
                  blink_delay = 250;
                  volume_mil = 0;
                  digitalWrite(c_pin_red_led, HIGH);
                  create_buffer_censtar_6(volume_mil, unit_price, is_nozzel_up);
                }
                else{
                  digitalWrite(c_pin_red_led, LOW);
                  create_buffer_censtar_6(volume_mil, unit_price, is_nozzel_up);
                }
            }
            buffer_idx = 0;
            device_status = sending;
        break;
        case sending:
            if(repeat_count > 0){
                repeat_count--;
            }

            for(bite = 0; bite<PACKET_SIZE; bite++){
                send_byte_censtar_6(channel1_tx_buffer[buffer_idx + bite]);
            }

            device_status = waiting_for_button_press;
            ts = millis();
        break;
        default:
        break;
    }
  
    static bool green_led_value = HIGH;
    static unsigned long ts_green_led_blink = 0;
    if(is_running_button_pressed){
      if((millis() - ts_green_led_blink) > 75){
        ts_green_led_blink = millis();
        if(green_led_value){
          digitalWrite(c_pin_green_led, HIGH);
          green_led_value = LOW;
        }
        else{
          digitalWrite(c_pin_green_led, LOW);
          green_led_value = HIGH;
        }
      }
    }
}

// void loop()
// {
//     while (digitalRead(c_pin_button))
//         ; // waiting for button press
//     delay(100);
//     while (!digitalRead(c_pin_button))
//         ; // waiting fro release
//     Serial.println("Sending data...");
//     // walking in rows
//     for (size_t i = 0; i < sizeof(sdata1_tx_buffer) / PACKET_SIZE; i++)
//     {
//         // repeating packet for a row
//         for (size_t j = 0; j < repeat_packet; j++)
//         {
//             digitalWrite(c_pin_green_led, HIGH);
//             for (size_t k = 0; k < PACKET_SIZE; k++)
//             {
//                 send_byte(sdata1_tx_buffer[i][k]);
//             }
//             digitalWrite(c_pin_green_led, LOW);
//             delayMicroseconds(153);
//         }
//     }
//     Serial.println("Send data Done");
// }

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