
#include <SoftwareSerial.h>

const char c_pin_sdata1=3;
const char c_pin_sdata2=2;
const char c_pin_sclk=6;
const char c_pin_rclk=4;
const char c_pin_rclk_other=7; /*This pin is not used*/
const char c_pin_5v=5;

const char c_pin_serial_tx=A4;
const char c_pin_serial_rx=A5;
const char c_pin_button=A0;
const char c_pin_button_Tester=A1;

const char c_pin_red_led=13;
const char c_pin_green_led=12;

const char repeat_same_display_packet_for_times = 1;	

const unsigned char tx_buffer[1] = {
  0x33,
};

const unsigned char channel1_tx_buffer[] = {
    240 ,241 ,242 ,19 ,84 ,5 ,6 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,19 ,84 ,5 ,6 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,52 ,117 ,54 ,87 ,248 ,73 ,90 ,11 ,12 ,13,

    240 ,241 ,242 ,243 ,68 ,85 ,6 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,84 ,37 ,38 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,84 ,101 ,38 ,87 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,84 ,149 ,134 ,87 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,100 ,53 ,70 ,87 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,100 ,117 ,86 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,116 ,21 ,22 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,116 ,69 ,118 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,132 ,37 ,54 ,87 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,132 ,85 ,150 ,87 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,148 ,5 ,6 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,148 ,53 ,102 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,243 ,148 ,117 ,38 ,7 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,19 ,4 ,21 ,38 ,87 ,248 ,73 ,90 ,11 ,12 ,13 ,240 ,
    241 ,242 ,19 ,4 ,21 ,38 ,87 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,19 ,4 ,21 ,38 ,87 ,248 ,73 ,90 ,11 ,12 ,13 ,
    240 ,241 ,242 ,19 ,4 ,21 ,38 ,87 ,248 ,73 ,90 ,11 ,12 ,13
};

const unsigned char channel2_tx_buffer[] = {
    240 ,241 ,242 ,243 ,4 ,53 ,54 ,55 ,168 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,5 ,6 ,7 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,5 ,134 ,55 ,24 ,169 ,26 ,171 ,252 ,173,

    240 ,241 ,242 ,243 ,4 ,21 ,6 ,7 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,22 ,103 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,38 ,87 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,54 ,55 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,70 ,23 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,86 ,7 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,86 ,135 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,102 ,103 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,134 ,55 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,21 ,150 ,23 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,37 ,6 ,7 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,37 ,6 ,135 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,37 ,22 ,103 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,37 ,38 ,87 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,37 ,38 ,87 ,24 ,169 ,26 ,171 ,252 ,173 ,
    240 ,241 ,242 ,243 ,4 ,37 ,38 ,87 ,24 ,169 ,26 ,171 ,252 ,173,
    240 ,241 ,242 ,243 ,4 ,37 ,38 ,87 ,168 ,169 ,26 ,171 ,252 ,173
};

char only_emulate = 1;
SoftwareSerial mySerial(c_pin_serial_rx, c_pin_serial_tx); // RX, TX


void nop(){
    static volatile int i;
    i++;
}

void send_byte(unsigned char bite1, unsigned char bite2){
    for(int i=0; i<8; i++){
        unsigned char v1 = (bite1 & 0b10000000);
        unsigned char v2 = (bite2 & 0b10000000);
        bite1 = bite1 << 1;
        bite2 = bite2 << 1;
        v1 = v1 >> 7;
        v2 = v2 >> 7;

        digitalWrite(c_pin_sdata1, v1);
        digitalWrite(c_pin_sdata2, v2);
        delayMicroseconds(1);
        digitalWrite(c_pin_sclk, HIGH);
        delayMicroseconds(1);
        digitalWrite(c_pin_sclk, LOW); 
        delayMicroseconds(10);
    }
  
    digitalWrite(c_pin_rclk, HIGH);
    delayMicroseconds(9);
    digitalWrite(c_pin_rclk, LOW);  
    delayMicroseconds(5);
}

unsigned char error = 0; 
void setup() {
  
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
    mySerial.begin(9600);

    Serial.println("Buffer Size = " + String(sizeof(channel1_tx_buffer)));

    error = 0;
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

void loop() {

    if(device_status_prev != device_status){
        device_status_prev = device_status;

        if(device_status == waiting_for_button_press){
            blink_delay = 500;
        }
    }
 
    switch(device_status){
        case waiting_for_button_press:
            if(digitalRead(c_pin_button) == HIGH){
                delay(50);
                while(digitalRead(c_pin_button) == HIGH);
                only_emulate = 1;
                device_status = sending;
                buffer_idx = 0; 
                error = 0;
                blink_delay = 175;
                repeat_count = repeat_same_display_packet_for_times;
                Serial.println("Restarting Sequence...");
            }
            else if(digitalRead(c_pin_button_Tester) == LOW){
                Serial.println("Button Pressed...");
                delay(50);
                while(digitalRead(c_pin_button_Tester) == LOW);
                Serial.println("Button Released...");
                device_status = sending;
                buffer_idx = 0; 
                error = 0;
                blink_delay = 250;
                repeat_count = repeat_same_display_packet_for_times;
                only_emulate = 0;
                Serial.println("Restarting Sequence...");
            }
        break;
        case sending:
            if(repeat_count > 0){
                repeat_count--;
            }

            for(bite = 0; bite<8; bite++){
                send_byte(channel1_tx_buffer[buffer_idx + bite], channel2_tx_buffer[buffer_idx + bite]);
            }
            last_sent_buffer_idx = buffer_idx;
            buffer_idx += 8;

            device_status = waiting_for_7ms;
            ts = millis();
        break;
        case waiting_for_7ms:
            if(millis() - ts >= 7){
                for(bite = 0; bite<6; bite++){
                    send_byte(channel1_tx_buffer[buffer_idx + bite], channel2_tx_buffer[buffer_idx + bite]);        
                }
                
                buffer_idx += 6;
                device_status = waiting_for_900ms;
                ts = millis();
            }
        break;
        case waiting_for_900ms:
            if(millis() - ts >= 700){
                ts = millis();
                if(repeat_count > 0){
                    device_status = sending;
                    buffer_idx = last_sent_buffer_idx;	
                    Serial.println("Repeating 28 bytes, Buffer Index = " + String(last_sent_buffer_idx));
                }
                else if(only_emulate==1){
                    if((buffer_idx + 14) > sizeof(channel1_tx_buffer)){
                        device_status = waiting_for_button_press;
                        Serial.println("Buffer complete..., waiting for button press");
                        error = 0xFF;
                    }
                    else{
                        device_status = sending;
                        repeat_count = repeat_same_display_packet_for_times;
                        Serial.println("Sending next 28 bytes, Buffer Index = " + String(buffer_idx));
                    }
                }
                else{
                    device_status = verify_data;
                    Serial.println("Verifying sent packet...");
                }
            }
        break;
        case verify_data:
            Serial.print("Sent Buffer A:");
            for(int j=0; j<14; j++){
                Serial.print(channel2_tx_buffer[last_sent_buffer_idx + j], HEX);
                Serial.print(" ");
            }
            Serial.println();
            Serial.print("Received Buffer A:");
            for(int j=0; j<14; j++){
                Serial.print(verify_buffer[j*2], HEX);
                Serial.print(" ");
            }
            Serial.println();      
            Serial.print("Sent Buffer B:");
            for(int j=0; j<14; j++){
                Serial.print(channel1_tx_buffer[last_sent_buffer_idx + j], HEX);
                Serial.print(" ");
            }
            Serial.println();
            Serial.print("Received Buffer B:");
            for(int j=0; j<14; j++){
                Serial.print(verify_buffer[j*2+1], HEX);
                Serial.print(" ");
            }
            Serial.println();

            for(int i=0; i<14; i++){
                if(channel2_tx_buffer[last_sent_buffer_idx + i] != verify_buffer[i*2]){
                    error = 1;
                    Serial.print("Error occured on A at " + String(i));
                    break;
                }
                else if(channel1_tx_buffer[last_sent_buffer_idx + i] != verify_buffer[i*2+1]){
                    Serial.print("Error occured on B at " + String(i));
                    error = 1;
                    break;
                }
            }

            if(error == 1){
                device_status = waiting_for_button_press;
            }
            else{
                if((buffer_idx + 14) > sizeof(channel1_tx_buffer)){
                    device_status = waiting_for_button_press;
                    Serial.println("Buffer complete..., waiting for button press");
                    error = 0xFF;
                }
                else{
                    device_status = sending;
                    repeat_count = repeat_same_display_packet_for_times;
                    Serial.println("Sending next 28 bytes, Buffer Index = " + String(buffer_idx));
                }
            }
        break;
    }

    if (mySerial.available()){

        unsigned char value = mySerial.read();
        switch(decoder_state){
            case 0:
                if(value == 's'){
        //          Serial.println("S");
                decoder_state++;
                }
                else{
                decoder_state = 0;
                }
            break;
            case 1:
                if(value == 't'){
        //          Serial.println("T");
                    decoder_state++;
                }
                else{
                    decoder_state = 0;
                }
            break;
            case 2:
                if(value == 'x'){
        //          Serial.println("X");
                    decoder_state++;
                }
                else{
                    decoder_state = 0;
                }
            break;
            case 3:
        //          Serial.println("V1");
                    decoder_state++; //version 0
            break;
            case 4:
        //          Serial.println("V2");
                    decoder_state++; //version 1
            break;
            case 5:
                    if(input_sig_prev != value){
                        Serial.println("Input Sig = " + String(value, HEX));
                        input_sig_prev = value;
                    }
                    decoder_state++; //input signal data
                    verify_buffer_idx = 0;
            break;
            case 6:
                // Serial.println("D : " + String(verify_buffer_idx));
                verify_buffer[verify_buffer_idx++] = value;
                if(verify_buffer_idx == 28){
                    decoder_state = 0;
                


                //    Serial.print("Received Buffer LSB:");
                //    for(int j=0; j<30; j++){
                //        Serial.print(verify_buffer[j], HEX);
                //        Serial.print(" ");
                //    }
                //    Serial.println();

//                     for(int i=0; i<30; i++){
//                       char tmp_var = verify_buffer_inter[i];
//                       char tmp_var_msb = 0;
//                       for(int j=0; j<8; j++){
//                         tmp_var_msb = tmp_var_msb << 1;
//                         tmp_var_msb |= tmp_var & 0x01;
//                         tmp_var = tmp_var >> 1;                        
//                       }
//                       verify_buffer_inter[i] = tmp_var_msb;
//                     }

// //                    Serial.print("Received Buffer MSB:");
// //                    for(int j=0; j<30; j++){
// //                        Serial.print(verify_buffer_inter[j], HEX);
// //                        Serial.print(" ");
// //                    }
// //                    Serial.println();

//                     for(int i=0; i<30; i+=2){
//                       char tmp_var1 = verify_buffer_inter[i];
//                       char tmp_var2 = verify_buffer_inter[i+1];
//                       verify_buffer[(tmp_var1 & 0x0F)*2] = tmp_var1;
//                       verify_buffer[(tmp_var1 & 0x0F)*2 + 1] = tmp_var2;
//                     }
                    
// //                    Serial.print("Received Buffer*MSB:");
// //                    for(int j=0; j<30; j++){
// //                        Serial.print(verify_buffer[j], HEX);
// //                        Serial.print(" ");
// //                    }
// //                    Serial.println();
                    
//                     // Serial.print("A:");
//                     // for(int j=0; j<14; j++){
//                     //     Serial.print(verify_buffer[j*2], HEX);
//                     //     Serial.print(" ");
//                     // }
//                     // Serial.println("");
//                     // Serial.print("B:");
//                     // for(int j=0; j<14; j++){
//                     //     Serial.print(verify_buffer[j*2 + 1], HEX);
//                     //     Serial.print(" ");
//                     // }
//                     // Serial.println("");
//                     // Serial.println("");
                }
            break;
        }
    }

    if (mySerial.overflow()) {
        Serial.println("Software Serial Overflow!");
    }
  
//  Serial.println(error); 
    if(error == 0){ 
        digitalWrite(c_pin_red_led, LOW);
        static unsigned long ts_led = 0;
        static unsigned char led_state = 0;
        if(millis() - ts_led > blink_delay){
            ts_led = millis();
            if(led_state==0){    
                digitalWrite(c_pin_green_led, HIGH);
                led_state = 1;
            }
            else{
                digitalWrite(c_pin_green_led, LOW);
                led_state = 0;
            }
        }
    }
    else if(error == 1){
        digitalWrite(c_pin_red_led, HIGH);
        digitalWrite(c_pin_green_led, LOW);    
    }
    else if(error == 0xFF){
        digitalWrite(c_pin_red_led, LOW);
        digitalWrite(c_pin_green_led, HIGH);    
    }
   
}