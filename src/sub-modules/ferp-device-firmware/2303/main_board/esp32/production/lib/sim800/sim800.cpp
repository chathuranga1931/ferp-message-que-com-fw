#include "Arduino.h"
#include "board_2303.h"
#include "sim800.h"


void init_sim800()
{
    Serial.end();
    Serial.begin(9600);
    
    digitalWrite(EN_4G, HIGH);
}

void send_msg()
{
    Serial.print("AT\r\n");
    delay(100);
    
}
