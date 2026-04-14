#include <Arduino.h>
#include <ctype.h>

#define PACKET_SIZE 51

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

#define L 0x1C
#define P 0xCE
#define LL_A 0xEE
#define LL_n 0x2A
#define LL_1 0x60

// index digit, 7 segment code, 7 segment code with decimal point
#define CODE_DIGIT_MAP(XX) \
    XX(0, 0xFC, 0xFD)      \
    XX(1, 0x60, 0x61)      \
    XX(2, 0xDA, 0xDB)      \
    XX(3, 0xF2, 0xF3)      \
    XX(4, 0x66, 0x67)      \
    XX(5, 0xB6, 0xB7)      \
    XX(6, 0xBE, 0xBF)      \
    XX(7, 0xE0, 0xE1)      \
    XX(8, 0xFE, 0xFF)      \
    XX(9, 0xF6, 0xF7)      


#define CS_T 20
enum
{
    DELAY = 0, // adding delay
    RCLK,      // toggling RCLK (Chip Select) line
    DATA,      //  sending data byte
};

typedef enum
{
    IDX_VOLUME_START = 6,
    IDX_VOLUME_5 = IDX_VOLUME_START,
    IDX_VOLUME_4,
    IDX_VOLUME_3,
    IDX_VOLUME_2, // decimal point
    IDX_VOLUME_1,
    IDX_VOLUME_0,
    IDX_VOLUME_SIZE,

    IDX_TOTAL_START = 12,
    IDX_TOTAL_5 = IDX_TOTAL_START,
    IDX_TOTAL_4,
    IDX_TOTAL_3,
    IDX_TOTAL_2,
    IDX_TOTAL_1, // decimal point
    IDX_TOTAL_0,
    IDX_TOTAL_SIZE,

    IDX_UNIT_START = 25,
    IDX_UNIT_3 = IDX_UNIT_START,
    IDX_UNIT_2,
    IDX_UNIT_1, // decimal point
    IDX_UNIT_0,
    IDX_UNIT_SIZE,

    IDX_SELECT_L = IDX_TOTAL_5,
    IDX_SELECT_P = IDX_SELECT_L,
} byte_index_t;

typedef enum
{
    LL_IDX_LL_A = IDX_UNIT_3,
    LL_IDX_LL_n = IDX_UNIT_1,
    LL_IDX_LL_1 = IDX_UNIT_0,

    LL_IDX_TOT_LITERS_0 = IDX_VOLUME_0,
    LL_IDX_TOT_LITERS_1 = IDX_VOLUME_1,
    LL_IDX_TOT_LITERS_2 = IDX_VOLUME_2,
    LL_IDX_TOT_LITERS_3 = IDX_VOLUME_3,
    LL_IDX_TOT_LITERS_4 = IDX_VOLUME_4,
    LL_IDX_TOT_LITERS_5 = IDX_VOLUME_5,

    LL_IDX_TOT_LITERS_6 = IDX_TOTAL_0,
    LL_IDX_TOT_LITERS_7 = IDX_TOTAL_1,
    LL_IDX_TOT_LITERS_8 = IDX_TOTAL_2,
    LL_IDX_TOT_LITERS_9 = IDX_TOTAL_3,
    LL_IDX_TOT_LITERS_10 = IDX_TOTAL_4,
    LL_IDX_TOT_LITERS_11 = IDX_TOTAL_5,
} ll_byte_index_t;

typedef struct
{
    uint8_t mode;
    uint32_t data;
} data_t;

const uint8_t digit_map[] = {
#define XX(CODE, DIGIT, DIGIT_DECI) [CODE] = DIGIT,
    CODE_DIGIT_MAP(XX)
#undef XX
};

const uint8_t digit_deci_map[] = {
#define XX(CODE, DIGIT, DIGIT_DECI) [CODE] = DIGIT_DECI,
    CODE_DIGIT_MAP(XX)
#undef XX
};

const size_t row_data_len = PACKET_SIZE + 127;
const size_t row_data_size = sizeof(data_t) * row_data_len;

data_t sdata1_tx[row_data_len] = {
    {.mode = RCLK, .data = true}, // on clock
    {.mode = DELAY, .data = 500}, // add delay
    // control signals, send starting bytes
    {.mode = DATA, .data = 0x12},
    {.mode = DELAY, .data = 290},
    {.mode = RCLK, .data = false}, // off clock
    {.mode = DELAY, .data = 65},
    // segment data signals, first 0 - 11 data codes
    {.mode = DATA, .data = 0x00}, // IDX_VOLUME_5    6-index
    {.mode = DATA, .data = 0x00}, // IDX_VOLUME_4
    {.mode = DATA, .data = 0x00}, // IDX_VOLUME_3
    {.mode = DATA, .data = 0x61}, // IDX_VOLUME_2    1.
    {.mode = DATA, .data = 0xFC}, // IDX_VOLUME_1    0
    {.mode = DATA, .data = 0xFC}, // IDX_VOLUME_0    0
    {.mode = DATA, .data = 0x00}, // IDX_TOTAL_5
    {.mode = DATA, .data = 0x00}, // IDX_TOTAL_4
    {.mode = DATA, .data = 0x66}, // IDX_TOTAL_3     4
    {.mode = DATA, .data = 0xB6}, // IDX_TOTAL_2     5
    {.mode = DATA, .data = 0xBF}, // IDX_TOTAL_1     6.
    {.mode = DATA, .data = 0xFC}, // IDX_TOTAL_0     0
    // control signals, dummy bytes
    {.mode = DELAY, .data = 280},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 60},
    {.mode = DATA, .data = 0x13},
    {.mode = DELAY, .data = 275},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 25},
    // segment data signals, 12 - 15 data codes
    {.mode = DATA, .data = 0x66}, // IDX_UNIT_3      4
    {.mode = DATA, .data = 0xB6}, // IDX_UNIT_2      5
    {.mode = DATA, .data = 0xBF}, // IDX_UNIT_1      6.
    {.mode = DATA, .data = 0xFC}, // IDX_UNIT_0      0
    // control signals, dummy bytes
    {.mode = DELAY, .data = 310},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 450},
    {.mode = DATA, .data = 0xF0},
    {.mode = DELAY, .data = 400},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 20},
    // segment data signals, 16 - 16 data code
    {.mode = DATA, .data = 0xFE},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 310},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 2},
    // segment data signals, 17 - 17 data code
    {.mode = DATA, .data = 0xFE},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 340},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 0},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 1},
    // segment data signals, 18 - 18 data code
    {.mode = DATA, .data = 0xFE},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 280},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 1},
    // segment data signals, 19 - 19 data code
    {.mode = DATA, .data = 0xFE},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 350},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 0},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 1},
    // segment data signals, 20 - 20 data code
    {.mode = DATA, .data = 0xFD},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 250},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 1},
    // segment data signals, 21 - 21 data code
    {.mode = DATA, .data = 0xFD},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 350},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 0},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 1},
    // segment data signals, 22 - 22 data code
    {.mode = DATA, .data = 0xFB},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 300},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 1},
    // segment data signals, 23 - 23 data code
    {.mode = DATA, .data = 0xFB},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 355},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 355},
    {.mode = DATA, .data = 0x22},
    {.mode = DELAY, .data = 290},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = CS_T},
    // segment data signals, 24 - 31 data code
    {.mode = DATA, .data = 0x00},
    {.mode = DATA, .data = 0x00},
    {.mode = DATA, .data = 0x00},
    {.mode = DATA, .data = 0x00},
    {.mode = DATA, .data = 0x00},
    {.mode = DATA, .data = 0x01},
    {.mode = DATA, .data = 0xFB},
    {.mode = DATA, .data = 0xF9},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 240},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 240},
    {.mode = DATA, .data = 0x11},
    {.mode = DELAY, .data = 410},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 9},
    // segment data signals, 32 - 32 data code
    {.mode = DATA, .data = 0xC0},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 260},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 5},
    // segment data signals, 33 - 33 data code
    {.mode = DATA, .data = 0xC0},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 410},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 0},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 5},
    // segment data signals, 34 - 34 data code
    {.mode = DATA, .data = 0xA0},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 325},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 5},
    // segment data signals, 35 - 35 data code
    {.mode = DATA, .data = 0xA0},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 335},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 5},
    // segment data signals, 36 - 36 data code
    {.mode = DATA, .data = 0x60},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 345},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 5},
    // segment data signals, 37 - 37 data code
    {.mode = DATA, .data = 0x60},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 415},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 0},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 5},
    // segment data signals, 39 - 39 data code
    {.mode = DATA, .data = 0xE0},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 440},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 18},
    // segment data signals, 40 - 40 data code
    {.mode = DATA, .data = 0xE0},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 280},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 250},
    // segment data signals, 41 - 41 data code
    {.mode = DATA, .data = 0x21},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 280},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 18},
    // segment data signals, 42 - 42 data code
    {.mode = DATA, .data = 0x01},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 320},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 0},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 2},
    // segment data signals, 43 - 43 data code
    {.mode = DATA, .data = 0x02},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 400},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 0},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 2},
    // segment data signals, 44 - 44 data code
    {.mode = DATA, .data = 0x04},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 250},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 1},
    // segment data signals, 45 - 45 data code
    {.mode = DATA, .data = 0x08},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 370},
    {.mode = RCLK, .data = true},
    {.mode = DELAY, .data = 1},
    {.mode = RCLK, .data = false},
    {.mode = DELAY, .data = 1},
    // segment data signals, 46 - 46 data code
    {.mode = DATA, .data = 0x10},
    // control signals, dummy bytes
    {.mode = DELAY, .data = 220},
    {.mode = RCLK, .data = true}};

const size_t repeat_packet = 10;

const float volume_l_step = 1.09; // 0.01
const float unit_price = 456.1;   // 0.1
const uint64_t totalizer_value = 123456789123ULL; //0.001, totalizer has 3 decimal points

float volume_l = 0; // 0.01

void send_byte(uint8_t bite1);

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

void create_buffer_wayne6(float volume, float price)
{
    uint32_t unit_01 = (uint32_t)(price * 10);           // 0.1
    uint32_t total_01 = (uint32_t)(volume * price * 10.0L); // 0.1
    uint32_t volume_001 = (uint32_t)(volume * 100);      // 0.01

    Serial.println("VOL: " + String(volume_001 / 100.0, 2) + "\t UNIT: " + String(unit_01 / 10.0, 1) + "\t Total : " + String(total_01 / 10.0, 1));

    // load unit price 0.1
    for (size_t i = IDX_UNIT_0; i >= IDX_UNIT_START; i--)
    {
        if (unit_01)
        {
            if (i == IDX_UNIT_1)
                sdata1_tx[i].data = digit_deci_map[unit_01 % 10];
            else
                sdata1_tx[i].data = digit_map[unit_01 % 10];
            unit_01 /= 10;
        }
        else
        {
            sdata1_tx[i].data = 0x00; // blank for the rest
        }
    }

    // load total price 0.1
    for (size_t i = IDX_TOTAL_0; i >= IDX_TOTAL_START; i--)
    {
        if (total_01)
        {
            if (i == IDX_TOTAL_1)
                sdata1_tx[i].data = digit_deci_map[total_01 % 10];
            else
                sdata1_tx[i].data = digit_map[total_01 % 10];
            total_01 /= 10;
        }
        else
        {
            sdata1_tx[i].data = 0x00; // blank for the rest
        }
    }

    // load liters 0.01
    for (size_t i = IDX_VOLUME_0; i >= IDX_VOLUME_START; i--)
    {
        if (volume_001)
        {
            if (i == IDX_VOLUME_2)
                sdata1_tx[i].data = digit_deci_map[volume_001 % 10];
            else
                sdata1_tx[i].data = digit_map[volume_001 % 10];
            volume_001 /= 10;
        }
        else
        {
            sdata1_tx[i].data = 0x00; // blank for the rest
        }
    }
}

void create_buffer_wayne6_ll(uint64_t total_ll)
{
    uint64_t total_0001 = (uint64_t)(total_ll);
    char str[100] = {};
    Serial.println("LL: " + String(uintToStr(total_0001, str)));

    //set Totaliser detect charactors
    sdata1_tx[LL_IDX_LL_A].data = LL_A;
    sdata1_tx[LL_IDX_LL_n].data = LL_n;
    sdata1_tx[LL_IDX_LL_1].data = LL_1;

    //load digits for liters section 0.001
    for (size_t i = IDX_VOLUME_0; i >= IDX_VOLUME_START; i--)
    {
        if (total_0001)
        {
            if (i == IDX_VOLUME_3)
                sdata1_tx[i].data = digit_deci_map[total_0001 % 10];
            else
                sdata1_tx[i].data = digit_map[total_0001 % 10];
            total_0001 /= 10;
        }
        else
        {
            sdata1_tx[i].data = 0x00; // blank for the rest
        }
    }

    //load digits for total section
    for (size_t i = IDX_TOTAL_0; i >= IDX_TOTAL_START; i--)
    {
        if (total_0001)
        {
            sdata1_tx[i].data = digit_map[total_0001 % 10];
            total_0001 /= 10;
        }
        else
        {
            sdata1_tx[i].data = 0x00; // blank for the rest
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
    digitalWrite(c_pin_green_led, LOW);

    set_pin_sdata1(false);
    set_pin_sclk(true);
    set_pin_rclk(true);

    Serial.begin(115200);
    // Serial.println("Packet Size = " + String(PACKET_SIZE) + " Row Len = " + String(row_data_len) + " Rows = " + String(sizeof(sdata1_tx_buffer) / row_data_size));
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
    


    // while (digitalRead(c_pin_button))
    //     ; // waiting for button press
    // delay(100);
    // while (!digitalRead(c_pin_button))
    //     ; // waiting fro release
    // Serial.println("Sending data...");

pump:
    Serial.println("Sending Pump data...");
    volume_l = 0.0;
    while (1)
    {
        create_buffer_wayne6(volume_l, unit_price);
        volume_l += volume_l_step;

        // repeating packet for a row
        for (size_t j = 0; j < repeat_packet; j++)
        {
            digitalWrite(c_pin_green_led, HIGH);
            for (size_t k = 0; k < row_data_len; k++)
            {
                switch (sdata1_tx[k].mode)
                {
                case DELAY:
                    delayMicroseconds(sdata1_tx[k].data);
                    break;
                case RCLK:
                    set_pin_rclk(sdata1_tx[k].data);
                    break;
                case DATA:
                    send_byte(sdata1_tx[k].data);
                    break;
                default:
                    break;
                }
            }
            digitalWrite(c_pin_green_led, LOW);
            delay(82);

            if (!digitalRead(c_pin_button)) // skip sending if button is pressed
            {
                delay(100); // remove debounce
                while (!digitalRead(c_pin_button))
                    ;       // waiting for release
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
    }
totaliser:
    Serial.println("Sending Totaliser data...");
    create_buffer_wayne6_ll(totalizer_value);
    while (1)
    {
        digitalWrite(c_pin_green_led, HIGH);
        for (size_t k = 0; k < row_data_len; k++)
        {
            switch (sdata1_tx[k].mode)
            {
            case DELAY:
                delayMicroseconds(sdata1_tx[k].data);
                break;
            case RCLK:
                set_pin_rclk(sdata1_tx[k].data);
                break;
            case DATA:
                send_byte(sdata1_tx[k].data);
                break;
            default:
                break;
            }
        }
        digitalWrite(c_pin_green_led, LOW);
        delay(82);

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

void send_byte(uint8_t bite1)
{
    for (int i = 0; i < 8; i++)
    {
        uint8_t v1 = (bite1 & 0b10000000);
        bite1 = bite1 << 1;
        v1 = v1 >> 7;
        set_pin_sdata1(v1);
        set_pin_sclk(false);
        delayMicroseconds(10);
        set_pin_sclk(true);
        delayMicroseconds(10);
    }
    set_pin_sdata1(false);
}
