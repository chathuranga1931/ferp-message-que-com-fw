#include <Arduino.h>

#define PACKET_SIZE 16

// #define set_pin_sdata1(level) level ? PIOC->PIO_SODR = PIO_PC28 : PIOC->PIO_CODR = PIO_PC28;
// #define set_pin_sclk(level) level ? PIOC->PIO_SODR = PIO_PC25 : PIOC->PIO_CODR = PIO_PC25;
// #define set_pin_rclk(level) level ? PIOC->PIO_SODR = PIO_PC24 : PIOC->PIO_CODR = PIO_PC24;

#define set_pin_sdata1(level) digitalWrite(c_pin_sdata1, level)
#define set_pin_sclk(level) digitalWrite(c_pin_sclk, level)
#define set_pin_rclk(level) digitalWrite(c_pin_rclk, level)

const uint32_t c_pin_sdata1 = 3; // PC28
const uint32_t c_pin_sclk = 6;   // PC25
const uint32_t c_pin_rclk = 4;   // PC24

const uint32_t c_pin_red_led = 10;
const uint32_t c_pin_green_led = 9;
const uint32_t c_pin_button = A0;    // pump start signal
const uint32_t c_pin_button_ll = A1; // LL totalizer send signal

typedef enum
{
    DIS_CHARACTOR_0 = 0,
    DIS_CHARACTOR_1,
    DIS_CHARACTOR_2,
    DIS_CHARACTOR_3,
    DIS_CHARACTOR_4,
    DIS_CHARACTOR_5,
    DIS_CHARACTOR_6,
    DIS_CHARACTOR_7,
    DIS_CHARACTOR_8,
    DIS_CHARACTOR_9,
    DIS_CHARACTOR_L,
    DIS_CHARACTOR_H,
    DIS_CHARACTOR_P,
    DIS_CHARACTOR_A,
    DIS_CHARACTOR_DASH,
    DIS_CHARACTOR_BLANK,
} display_charactor_t;

typedef enum
{
    IDX_UNIT_START = 0,
    IDX_UNIT_0 = IDX_UNIT_START,
    IDX_UNIT_1,
    IDX_UNIT_2,
    IDX_UNIT_3,
    IDX_UNIT_SIZE,

    IDX_TOTAL_START = 4,
    IDX_TOTAL_0 = IDX_TOTAL_START,
    IDX_TOTAL_1,
    IDX_TOTAL_2,
    IDX_TOTAL_3,
    IDX_TOTAL_4,
    IDX_TOTAL_5,
    IDX_TOTAL_SIZE,

    IDX_VOLUME_START = 10,
    IDX_VOLUME_0 = IDX_VOLUME_START,
    IDX_VOLUME_1,
    IDX_VOLUME_2,
    IDX_VOLUME_3,
    IDX_VOLUME_4,
    IDX_VOLUME_5,
    IDX_VOLUME_SIZE,

    IDX_SELECT_L = IDX_TOTAL_5,
    IDX_SELECT_P = IDX_SELECT_L,

} byte_index_t;

typedef enum
{
    LL_IDX_LL_1 = IDX_TOTAL_4,
    LL_IDX_LL_2 = IDX_TOTAL_5,

    LL_IDX_TOT_LITERS_0 = IDX_VOLUME_0,
    LL_IDX_TOT_LITERS_1,
    LL_IDX_TOT_LITERS_2,
    LL_IDX_TOT_LITERS_3,
    LL_IDX_TOT_LITERS_4,
    LL_IDX_TOT_LITERS_5,

    LL_IDX_TOT_LITERS_6 = IDX_TOTAL_0,
    LL_IDX_TOT_LITERS_7,
    LL_IDX_TOT_LITERS_8,
    LL_IDX_TOT_LITERS_9,
} ll_byte_index_t;

typedef union
{
    struct
    {
        uint8_t IDX : 4; // first 4 bits
        uint8_t DIG : 4; // last 4 bits
    };
    uint8_t u8int;
} data_t;

data_t sdata1_tx[PACKET_SIZE] = {
    {.u8int = 0x20},
    {.u8int = 0x31},
    {.u8int = 0x22},
    {.u8int = 0x43},
    {.u8int = 0x24},
    {.u8int = 0x95},
    {.u8int = 0x36},
    {.u8int = 0x57},
    {.u8int = 0x28},
    {.u8int = 0xF9},
    {.u8int = 0x0A},
    {.u8int = 0x0B},
    {.u8int = 0x0C},
    {.u8int = 0x6D},
    {.u8int = 0xFE},
    {.u8int = 0xFF},
};

const size_t repeat_packet = 100;

const float volume_l_step = 2.0; // 0.001
const float unit_price = 423.2; // 0.1
const uint64_t totalizer_value = 1234567891ULL; //0.001, totalizer has 3 decimal points

float volume_l = 0; // 0.001

void send_byte(unsigned char bite1);

char * uintToStr( const uint64_t num, char *str )
{
  uint8_t i = 0;
  uint64_t n = num;
  
  do
    i++;
  while ( n /= 10 );
  
  str[i] = '\0';
  n = num;
 
  do
    str[--i] = ( n % 10 ) + '0';
  while ( n /= 10 );

  return str;
}

void create_buffer_censtar_6(float volume, float price)
{
    uint32_t unit_01 = (uint32_t)(price * 10.0L);           // 0.1
    uint32_t total_01 = (uint32_t)(volume * price * 10.0L); // 0.1
    uint32_t volume_0001 = (uint32_t)(volume * 1000);      // 0.001

    Serial.println("VOL: " + String(volume_0001 / 1000.0, 3) + "\t UNIT: " + String(unit_01 / 10.0, 1) + "\t Total : " + String(total_01 / 10.0, 1));

    // load unit price 0.1
    for (size_t i = IDX_UNIT_0; i < IDX_UNIT_SIZE; i++)
    {
        if(unit_01)
        {
            sdata1_tx[i].DIG = unit_01%10;
            unit_01 /= 10;
        }
        else if(i == IDX_UNIT_0)
        {
            sdata1_tx[i].DIG = 0x0;
        }
        else
        {
            sdata1_tx[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load total price 0.1
    for (size_t i = IDX_TOTAL_0; i < IDX_TOTAL_SIZE; i++)
    {
        if(total_01)
        {
            sdata1_tx[i].DIG = total_01%10;
            total_01 /= 10;
        }
        else if(i == IDX_TOTAL_0)
        {
            sdata1_tx[i].DIG = 0x0;
        }
        else
        {
            sdata1_tx[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load liters 0.001
    for (size_t i = IDX_VOLUME_0; i < IDX_VOLUME_SIZE; i++)
    {
        if(volume_0001)
        {
            sdata1_tx[i].DIG = volume_0001%10;
            volume_0001 /= 10;
        }
        else if(i == IDX_VOLUME_0)
        {
            sdata1_tx[i].DIG = 0x0;
        }
        else
        {
            sdata1_tx[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }
}

void create_buffer_censtar_7_ll(uint64_t total_ll, float price)
{
    uint32_t unit_01 = (uint32_t)(price * 10.0L);           // 0.1
    uint64_t total_0001 = (uint64_t)(total_ll);
    char str[100] = {};
    Serial.println("LL: " + String(uintToStr(total_0001, str)));

    //set Totaliser detect charactors
    sdata1_tx[LL_IDX_LL_1].DIG = DIS_CHARACTOR_L;
    sdata1_tx[LL_IDX_LL_2].DIG = DIS_CHARACTOR_L;
    
    // load unit price 0.01
    for (size_t i = IDX_UNIT_0; i < IDX_UNIT_SIZE; i++)
    {
        if(unit_01)
        {
            sdata1_tx[i].DIG = unit_01%10;
            unit_01 /= 10;
        }
        else if(i == IDX_UNIT_0)
        {
            sdata1_tx[i].DIG = 0x0;
        }
        else
        {
            sdata1_tx[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    //load digits for liters section 0.001
    for (size_t i = IDX_VOLUME_0; i <= IDX_VOLUME_5; i++)
    {
        if (total_0001)
        {
            sdata1_tx[i].DIG = total_0001 % 10;
            total_0001 /= 10;
        }
        else
        {
            sdata1_tx[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load digits for total section
    for (size_t i = IDX_TOTAL_0; i <= IDX_TOTAL_3; i++)
    {
        if (total_0001)
        {
            sdata1_tx[i].DIG = total_0001 % 10;
            total_0001 /= 10;
        }
        else
        {
            sdata1_tx[i].DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }
}

void setup()
{
    pinMode(c_pin_sdata1, OUTPUT);
    pinMode(c_pin_sclk, OUTPUT);
    pinMode(c_pin_rclk, OUTPUT);
    pinMode(c_pin_red_led, OUTPUT);
    pinMode(c_pin_green_led, OUTPUT);
    pinMode(c_pin_button, INPUT_PULLUP);
    pinMode(c_pin_button_ll, INPUT_PULLUP);

    set_pin_sdata1(false);
    set_pin_sclk(true);
    set_pin_rclk(false);

    Serial.begin(115200);
    // Serial.println("Packet Size = " + String(PACKET_SIZE) + " Row Size = " + String(sizeof(sdata1_tx_buffer) / PACKET_SIZE));
}

void loop()
{
    while (!digitalRead(c_pin_button)); // waiting for release
    while (!digitalRead(c_pin_button_ll)); // waiting for release

    Serial.println("Waiting for button A0 - Pump | A1 - Totaliser");

    while (1)
    {
        if(!digitalRead(c_pin_button))
        {
            delay(100);
            while (!digitalRead(c_pin_button)); // waiting for release
            goto pump;
        }
        if(!digitalRead(c_pin_button_ll))
        {
            delay(100);
            while (!digitalRead(c_pin_button_ll)); // waiting for release
            goto totaliser;
        }
    }

pump:
    volume_l = 0;

    while (1)
    {
        create_buffer_censtar_6(volume_l, unit_price);
        volume_l += volume_l_step;
        // repeating packet for a row
        for (size_t j = 0; j < repeat_packet; j++)
        {
            digitalWrite(c_pin_green_led, HIGH);
            for (size_t k = 0; k < PACKET_SIZE; k++)
            {
                send_byte(sdata1_tx[k].u8int);
            }
            digitalWrite(c_pin_green_led, LOW);
            delayMicroseconds(153);
        }

        if (!digitalRead(c_pin_button)) // skip sending if button is pressed
        {
            delay(100); // remove debounce
            while (!digitalRead(c_pin_button));       // waiting for release
            delay(100); // remove debounce
            goto end;
        }
        if (!digitalRead(c_pin_button_ll)) // skip sending if button is pressed
        {
            delay(100); // remove debounce
            while (!digitalRead(c_pin_button_ll));       // waiting for release
            delay(100); // remove debounce
            goto end;
        }
    }
    
totaliser:
    create_buffer_censtar_7_ll(totalizer_value, unit_price);
    while (1)
    {
        digitalWrite(c_pin_green_led, HIGH);
        for (size_t k = 0; k < PACKET_SIZE; k++)
        {
            send_byte(sdata1_tx[k].u8int);
        }
        digitalWrite(c_pin_green_led, LOW);
        delayMicroseconds(153);
        
        if (!digitalRead(c_pin_button)) // skip sending if button is pressed
        {
            delay(100); // remove debounce
            while (!digitalRead(c_pin_button));       // waiting for release
            delay(100); // remove debounce
            goto end;
        }
        if (!digitalRead(c_pin_button_ll)) // skip sending if button is pressed
        {
            delay(100); // remove debounce
            while (!digitalRead(c_pin_button_ll));       // waiting for release
            delay(100); // remove debounce
            goto end;
        }
    }
end:
    Serial.println("Send data Done");
}

void send_byte(unsigned char bite1)
{
    for (int i = 0; i < 8; i++)
    {
        unsigned char v1 = (bite1 & 0b10000000);
        bite1 = bite1 << 1;
        v1 = v1 >> 7;
        set_pin_sdata1(v1);
        delayMicroseconds(1);
        set_pin_sclk(false);
        delayMicroseconds(15);
        set_pin_sclk(true);
        if (i == 7) //at the last bit, toggle rclk
        {
            delayMicroseconds(9);
            set_pin_rclk(true);
            delayMicroseconds(14);
            set_pin_rclk(false);
            delayMicroseconds(48);
        }
        else
        {
            delayMicroseconds(36);
        }
    }
}