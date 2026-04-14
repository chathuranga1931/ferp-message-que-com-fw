#include <Arduino.h>

#define PACKET_SIZE 14
#define PACKET_SKIP_RCLK_LOW_ID 0xD
#define PACKET_DELAY_IDX_LL 0xA

#if defined(ARDUINO_SAM_DUE)
    #define set_pin_rclk(level) level ? PIOC->PIO_SODR = PIO_PC26 : PIOC->PIO_CODR = PIO_PC26;
    #define set_pin_sclk(level) level ? PIOC->PIO_SODR = PIO_PC24 : PIOC->PIO_CODR = PIO_PC24;
    #define set_pin_sdata1(level) level ? PIOC->PIO_SODR = PIO_PC28 : PIOC->PIO_CODR = PIO_PC28;
    #define set_pin_sdata2(level) level ? PIOB->PIO_SODR = PIO_PB25 : PIOB->PIO_CODR = PIO_PB25;
#else
    #define set_pin_sdata1(level) digitalWrite(c_pin_sdata1, level)
    #define set_pin_sdata2(level) digitalWrite(c_pin_sdata2, level)
    #define set_pin_sclk(level) digitalWrite(c_pin_sclk, level)
    #define set_pin_rclk(level) digitalWrite(c_pin_rclk, level)
#endif

const uint32_t c_pin_sdata1 = 3; // PC28
const uint32_t c_pin_sdata2 = 2; 
const uint32_t c_pin_sclk = 6; // PC25
const uint32_t c_pin_rclk = 4; // PC24

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
    IDX_SELECT_L = 0,

    IDX_TOTAL_START = 0,
    IDX_TOTAL_7 = IDX_TOTAL_START,
    IDX_TOTAL_6,
    IDX_TOTAL_5,
    IDX_TOTAL_4,
    IDX_TOTAL_3,
    IDX_TOTAL_2,
    IDX_TOTAL_1,
    IDX_TOTAL_0,
    IDX_TOTAL_SIZE,

    IDX_UNIT_START = 8,
    IDX_UNIT_5 = IDX_UNIT_START,
    IDX_UNIT_4,
    IDX_UNIT_3,
    IDX_UNIT_2,
    IDX_UNIT_1,
    IDX_UNIT_0,
    IDX_UNIT_SIZE
} ab_byte_index_t;

typedef enum
{
    IDX_SELECT_P = 0,

    IDX_LITERS_START = 1,
    IDX_LITERS_6 = IDX_LITERS_START,
    IDX_LITERS_5,
    IDX_LITERS_4,
    IDX_LITERS_3,
    IDX_LITERS_2,
    IDX_LITERS_1,
    IDX_LITERS_0,
    IDX_LITERS_SIZE,

    IDX_START_STOP = 8,
} bb_byte_index_t;

typedef enum
{
    LL_IDX_LL_1 = IDX_TOTAL_6,
    LL_IDX_LL_2 = IDX_TOTAL_7,

	LL_IDX_TOT_LITERS_0 = IDX_UNIT_0,
	LL_IDX_TOT_LITERS_1 = IDX_UNIT_1,
	LL_IDX_TOT_LITERS_2 = IDX_UNIT_2,
	LL_IDX_TOT_LITERS_3 = IDX_UNIT_3,
	LL_IDX_TOT_LITERS_4 = IDX_UNIT_4,
	LL_IDX_TOT_LITERS_5 = IDX_UNIT_5,
} ab_ll_byte_index_t;

typedef enum
{
	LL_IDX_TOT_LITERS_6  = IDX_LITERS_0,
	LL_IDX_TOT_LITERS_7  = IDX_LITERS_1,
	LL_IDX_TOT_LITERS_8  = IDX_LITERS_2,
	LL_IDX_TOT_LITERS_9  = IDX_LITERS_3,
	LL_IDX_TOT_LITERS_10 = IDX_LITERS_4,
	LL_IDX_TOT_LITERS_11 = IDX_LITERS_5,
	LL_IDX_TOT_LITERS_12 = IDX_LITERS_6,
} bb_ll_byte_index_t;

typedef union
{
    struct
    {
        uint8_t IDX : 4; // first 4 bits
        uint8_t DIG : 4; // last 4 bits
    };
    uint8_t u8int;
} u8int_t;

typedef struct 
{
    uint8_t idx; //delay bit 0 ~ 7, 8 for disable
    bool level;
    uint32_t delay;
}bit_delay_t;
typedef struct 
{
    uint32_t high_delay;
}rclk_delay_t;

typedef struct
{
    u8int_t data1;
    u8int_t data2;
    bit_delay_t bits;
    rclk_delay_t rclk;
} data_t;

data_t sdata_tx[PACKET_SIZE] = {
    {.data1 = {.u8int = 0xF0}, .data2 = {.u8int = 0xF0}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 14}}, // {{.IDX = 0x0, .DIG = 0xF}},
    {.data1 = {.u8int = 0xF1}, .data2 = {.u8int = 0xF1}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 14}}, // {{.IDX = 0x1, .DIG = 0xF}},
    {.data1 = {.u8int = 0xF2}, .data2 = {.u8int = 0xF2}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 14}}, // {{.IDX = 0x2, .DIG = 0x3}},
    {.data1 = {.u8int = 0x53}, .data2 = {.u8int = 0xF3}, .bits = {.idx = 3, .level = 0, .delay = 57}, .rclk = {.high_delay = 14}}, // {{.IDX = 0x3, .DIG = 0x7}},
    {.data1 = {.u8int = 0x84}, .data2 = {.u8int = 0x14}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 14}}, // {{.IDX = 0x4, .DIG = 0x9}},
    {.data1 = {.u8int = 0x95}, .data2 = {.u8int = 0x95}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 14}}, // {{.IDX = 0x5, .DIG = 0x0}},
    {.data1 = {.u8int = 0x56}, .data2 = {.u8int = 0x36}, .bits = {.idx = 6, .level = 0, .delay = 57}, .rclk = {.high_delay = 14}}, // {{.IDX = 0x6, .DIG = 0x0}},
    {.data1 = {.u8int = 0x67}, .data2 = {.u8int = 0x37}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 6436}}, // {{.IDX = 0x7, .DIG = 0x0}},
    {.data1 = {.u8int = 0xF8}, .data2 = {.u8int = 0x18}, .bits = {.idx = 5, .level = 0, .delay = 59}, .rclk = {.high_delay = 14}}, // {{.IDX = 0x8, .DIG = 0xF}},
    {.data1 = {.u8int = 0x39}, .data2 = {.u8int = 0xA9}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 14}}, // {{.IDX = 0x9, .DIG = 0x3}},
    {.data1 = {.u8int = 0x0A}, .data2 = {.u8int = 0x1A}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 14}}, // {{.IDX = 0xA, .DIG = 0x7}},
    {.data1 = {.u8int = 0x5B}, .data2 = {.u8int = 0xAB}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 105}}, // {{.IDX = 0xB, .DIG = 0x9}},
    {.data1 = {.u8int = 0x0C}, .data2 = {.u8int = 0xFC}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 14}}, // {{.IDX = 0xC, .DIG = 0x0}},
    {.data1 = {.u8int = 0x0D}, .data2 = {.u8int = 0xAD}, .bits = {.idx = 8, .level = 0, .delay = 0  }, .rclk = {.high_delay = 634}}, // {{.IDX = 0xD, .DIG = 0x0}}
};

const size_t repeat_packet = 10;
const size_t repeat_packet_ll = 10;

const float volume_l_step = 2.0;                  // 0.001
const float unit_price = 423.00;                  // 0.01
// const uint64_t totalizer_value = 7152180890ULL; // 0.001, totalizer has 3 digits
const uint64_t totalizer_value = 1234567891234ULL; // 0.001, totalizer has 3 digits

float volume_l = 0; // 0.001

void send_packet();
void send_byte(const uint8_t byte1, const uint8_t byte2, const bit_delay_t bit_delay, const rclk_delay_t rclk_delay);

char *uintToStr(const uint64_t num, char *str)
{
    uint8_t i = 0;
    uint64_t n = num;

    do
        i++;
    while (n /= 10);

    str[i] = '\0';
    n = num;

    do
        str[--i] = (n % 10) + '0';
    while (n /= 10);

    return str;
}

void create_buffer_hongyang_8(float volume, float price)
{
    uint32_t unit_001 = (uint32_t)(price * 100);              // 0.01
    uint32_t total_001 = (uint32_t)(volume * price * 100.0L); // 0.01
    uint32_t volume_0001 = (uint32_t)(volume * 1000);         // 0.001

    Serial.println("VOL: " + String(volume_0001 / 1000.0, 3) + "\t UNIT: " + String(unit_001 / 100.0, 2) + "\t Total : " + String(total_001 / 100.0, 2));

    // load unit price 0.01
    for (int i = IDX_UNIT_0; i >= IDX_UNIT_START; i--)
    {
        if (unit_001)
        {
            sdata_tx[i].data1.DIG = unit_001 % 10;
            unit_001 /= 10;
        }
        else if (i == IDX_UNIT_0)
        {
            sdata_tx[i].data1.DIG = 0x0;
        }
        else
        {
            sdata_tx[i].data1.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load total price 0.01
    for (int i = IDX_TOTAL_0; i >= IDX_TOTAL_START; i--)
    {
        if (total_001)
        {
            sdata_tx[i].data1.DIG = total_001 % 10;
            total_001 /= 10;
        }
        else if (i == IDX_TOTAL_0)
        {
            sdata_tx[i].data1.DIG = 0x0;
        }
        else
        {
            sdata_tx[i].data1.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load liters 0.001
    for (int i = IDX_LITERS_0; i >= IDX_LITERS_START; i--)
    {
        if (volume_0001)
        {
            sdata_tx[i].data2.DIG = volume_0001 % 10;
            volume_0001 /= 10;
        }
        else if (i == IDX_LITERS_0)
        {
            sdata_tx[i].data2.DIG = 0x0;
        }
        else
        {
            sdata_tx[i].data2.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }
}

void create_buffer_hongyang_8_dummy_ll()
{
    //fill sdata1 buffer
    sdata_tx[0].data1.DIG = 0xA;
    sdata_tx[1].data1.DIG = 0xF;
    sdata_tx[2].data1.DIG = 0xF;
    sdata_tx[3].data1.DIG = 0xF;
    sdata_tx[4].data1.DIG = 0xF;
    sdata_tx[5].data1.DIG = 0xF;
    sdata_tx[6].data1.DIG = 0xF;
    sdata_tx[7].data1.DIG = 0xF;
    sdata_tx[8].data1.DIG = 0x1;
    sdata_tx[9].data1.DIG = 0x8;
    sdata_tx[10].data1.DIG = 0x3;
    sdata_tx[11].data1.DIG = 0x3;
    sdata_tx[12].data1.DIG = 0x1;
    sdata_tx[13].data1.DIG = 0x3;

    //fill sdata2 buffer
    sdata_tx[0].data2.DIG = 0xF;
    sdata_tx[1].data2.DIG = 0xF;
    sdata_tx[2].data2.DIG = 0xF;
    sdata_tx[3].data2.DIG = 0xF;
    sdata_tx[4].data2.DIG = 0xF;
    sdata_tx[5].data2.DIG = 0x8;
    sdata_tx[6].data2.DIG = 0x0;
    sdata_tx[7].data2.DIG = 0x6;
    sdata_tx[8].data2.DIG = 0xF;
    sdata_tx[9].data2.DIG = 0xF;
    sdata_tx[10].data2.DIG = 0xF;
    sdata_tx[11].data2.DIG = 0x1;
    sdata_tx[12].data2.DIG = 0xF;
    sdata_tx[13].data2.DIG = 0xF;
}

void create_buffer_hongyang_8_ll(uint64_t total_ll, float price)
{
    // uint32_t unit_001 = (uint32_t)(price * 100); // 0.01
    uint64_t total_0001 = (uint64_t)(total_ll);
    char str[100] = {};
    Serial.println("LL: " + String(uintToStr(total_0001, str)));

    // set Totaliser detect charactors
    sdata_tx[LL_IDX_LL_1].data1.DIG = DIS_CHARACTOR_L;
    sdata_tx[LL_IDX_LL_2].data1.DIG = DIS_CHARACTOR_L;

    // load unit price 0.01
    for (int i = IDX_UNIT_0; i >= IDX_UNIT_5; i--)
    {
        if (total_0001)
        {
            sdata_tx[i].data1.DIG = total_0001 % 10;
            total_0001 /= 10;
        }
        else if(i == IDX_UNIT_0)
        {
            sdata_tx[i].data1.DIG = 0x0;
        }
        else
        {
            sdata_tx[i].data1.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load digits for liters section 0.001
    for (int i = IDX_LITERS_0; i >= IDX_LITERS_6; i--)
    {
        if (total_0001)
        {
            sdata_tx[i].data2.DIG = total_0001 % 10;
            total_0001 /= 10;
        }
        else if(i == IDX_LITERS_0)
        {
            sdata_tx[i].data2.DIG = 0x0;
        }
        else
        {
            sdata_tx[i].data2.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }
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
    pinMode(c_pin_button_ll, INPUT_PULLUP);
    digitalWrite(c_pin_green_led, LOW);

    set_pin_sdata1(false);
    set_pin_sdata2(false);
    set_pin_sclk(false);
    set_pin_rclk(false);

    Serial.begin(115200);
    // Serial.println("Packet Size = " + String(PACKET_SIZE) + " Rows = " + String(sizeof(sdata1_tx_buffer) / (sizeof(data_t) * PACKET_SIZE)));
}

void loop()
{
    while (!digitalRead(c_pin_button)); // waiting for release
    while (!digitalRead(c_pin_button_ll)); // waiting for release

    Serial.println("Waiting for button A0 - Pump | A1 - Totaliser");

    // goto pump;

    while (1)
    {
        if (!digitalRead(c_pin_button))
        {
            Serial.println("Waiting Release A0");
            delay(100);
            while (!digitalRead(c_pin_button))
                ; // waiting for release
            goto pump;
        }
        if (!digitalRead(c_pin_button_ll))
        {
            Serial.println("Waiting Release A1");
            delay(100);
            while (!digitalRead(c_pin_button_ll))
                ; // waiting for release
            goto totaliser;
        }
    }

pump:
    volume_l = 0.0;
    while (1)
    {
        create_buffer_hongyang_8(volume_l, unit_price);
        volume_l += volume_l_step;

        // repeating packet for a row
        for (size_t j = 0; j < repeat_packet; j++)
        {
            digitalWrite(c_pin_green_led, HIGH);
            send_packet();
            digitalWrite(c_pin_green_led, LOW);
        }
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
            while (!digitalRead(c_pin_button_ll))
                ;       // waiting for release
            delay(100); // remove debounce
            goto end;
        }
    }

totaliser:
    //start by sending price only first for couple of time
    create_buffer_hongyang_8(0, unit_price);
    for (size_t j = 0; j < repeat_packet_ll; j++)
    {
        digitalWrite(c_pin_green_led, HIGH);
        send_packet();
        digitalWrite(c_pin_green_led, LOW);
    }

    //send dummy byte before totaliser, at the end of the packet, keep RCLK high
    create_buffer_hongyang_8_dummy_ll();
    //send packet
    digitalWrite(c_pin_green_led, HIGH);
    send_packet();
    digitalWrite(c_pin_green_led, LOW);
    //add gap between next packet
    delayMicroseconds(7113);

    //send actual totaliser packet
    create_buffer_hongyang_8_ll(totalizer_value, unit_price);
    //send packet
    digitalWrite(c_pin_green_led, HIGH);
    send_packet();
    digitalWrite(c_pin_green_led, LOW);  

end:
    Serial.println("Send data Done");
}

#if defined(ARDUINO_SAM_DUE)
void delayNanoseconds(uint32_t nsec){
    /*
     * Based on Paul Stoffregen's implementation
     * for Teensy 3.0 (http://www.pjrc.com/)
     */
    if (nsec == 0) return;
    uint32_t n = nsec * (VARIANT_MCK / 3000000) / 1000;
    asm volatile(
        "L_%=delayNanoseconds:"       "\n\t"
        "subs   %0, #1"                 "\n\t"
        "bne    L_%=delayNanoseconds" "\n"
        : "+r" (n) :
    );
}
#else
void delayNanoseconds(uint32_t nsec)
{
    delayMicroseconds(nsec/1000);
}
#endif

void send_packet()
{
    for (size_t k = 0; k < PACKET_SIZE; k++)
    {
        send_byte(sdata_tx[k].data1.u8int, sdata_tx[k].data2.u8int, sdata_tx[k].bits, sdata_tx[k].rclk);
    }
    delayMicroseconds(29130);
}

void send_byte(const uint8_t byte1, const uint8_t byte2, const bit_delay_t bit_delay, const rclk_delay_t rclk_delay)
{
#define BIT_LOW_DELAY_US 3
#define BIT_HIGH_DELAY_US 2

    const uint8_t bit_map[] = {
        0b10000000,
        0b01000000,
        0b00100000,
        0b00010000,
        0b00001000,
        0b00000100,
        0b00000010,
        0b00000001
    };

    for (int i = 0; i < 8; i++)
    {
        const bool data1 = (bool)(byte1 & bit_map[i]);
        const bool data2 = (bool)(byte2 & bit_map[i]);

        if(i)
        {
            delayMicroseconds(14);
        }

        set_pin_sdata1(data1);
        set_pin_sdata2(data2);
        if(bit_delay.idx == i)
        {
            if (bit_delay.level)
            {
                delayMicroseconds(BIT_LOW_DELAY_US);
                set_pin_sclk(true);
                delayMicroseconds(bit_delay.delay);
            }
            else
            {
                delayMicroseconds(bit_delay.delay);
                set_pin_sclk(true);
                delayMicroseconds(BIT_HIGH_DELAY_US);
            }
        }
        else
        {
            delayMicroseconds(BIT_LOW_DELAY_US);
            set_pin_sclk(true);
            delayMicroseconds(BIT_HIGH_DELAY_US);
        }
        set_pin_sclk(false);
    }
    delayMicroseconds(7);
    set_pin_sdata1(false);
    set_pin_sdata2(false);
    set_pin_rclk(true);
    delayMicroseconds(rclk_delay.high_delay);
    set_pin_rclk(false);
    delayMicroseconds(59);
}