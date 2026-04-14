
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

const char c_pin_red_led=13;
const char c_pin_green_led=12;

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

SoftwareSerial mySerial(c_pin_serial_rx, c_pin_serial_tx); // RX, TX


void nop(){
  static volatile int i;
  i++;
}

void send_byte(unsigned char bite1, unsigned char bite2){
  for(int i=0; i<8; i++){
    unsigned char v1 = (bite1 & 0x01);
    unsigned char v2 = (bite2 & 0x01);
    bite1 = bite1 >> 1;    
    bite2 = bite2 >> 1;    
    
    digitalWrite(c_pin_sdata1, v1);
    digitalWrite(c_pin_sdata2, v2);
    delayMicroseconds(3);
    digitalWrite(c_pin_sclk, HIGH);
    delayMicroseconds(2);
    digitalWrite(c_pin_sclk, LOW); 
    delayMicroseconds(25);
  }
  
  digitalWrite(c_pin_rclk, HIGH);
  delayMicroseconds(14);
  digitalWrite(c_pin_rclk, LOW);  
  delayMicroseconds(64);
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
  pinMode(c_pin_serial_rx, INPUT);
  pinMode(c_pin_serial_tx, OUTPUT);
  
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
unsigned long ts = 0;
unsigned char bite = 0;

unsigned int buffer_idx = 0;
unsigned char verify_buffer[100] = {0};

void loop() {
 
  switch(device_status){
    case waiting_for_button_press:
      if(digitalRead(c_pin_button) == HIGH){
        delay(50);
        while(digitalRead(c_pin_button) == HIGH);
        device_status = sending;
        buffer_idx = 0; 
        error = 0;
        Serial.println("Restarting Sequence...");
      }
    break;
    case sending:
      for(bite = 0; bite<8; bite++){
        send_byte(channel1_tx_buffer[buffer_idx + bite], channel2_tx_buffer[buffer_idx + bite]);
      }
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

      if((buffer_idx + 14) > sizeof(channel1_tx_buffer)){
        device_status = waiting_for_button_press;        
        Serial.println("Buffer complete..., waiting for button press");
      }
      else if(millis() - ts >= 900){
        ts = millis();
        device_status = sending;
        Serial.println("Sending next 28 bytes, Buffer Index = " + String(buffer_idx));
      }
    break;
  } 
}
