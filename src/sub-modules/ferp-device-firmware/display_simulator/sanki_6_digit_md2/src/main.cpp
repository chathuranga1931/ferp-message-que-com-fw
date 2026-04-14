#include <Arduino.h>

#define PACKET_SIZE 17

#define set_pin_sdata1(level) level ? PIOC->PIO_SODR = PIO_PC28 : PIOC->PIO_CODR = PIO_PC28;
// #define set_pin_sdata2(level) level ? PIOB->PIO_SODR = PIO_PB25 : PIOB->PIO_CODR = PIO_PB25;
#define set_pin_sclk(level) level ? PIOC->PIO_SODR = PIO_PC24 : PIOC->PIO_CODR = PIO_PC24;
#define set_pin_rclk(level) level ? PIOC->PIO_SODR = PIO_PC26 : PIOC->PIO_CODR = PIO_PC26;

// #define set_pin_sdata1(level) digitalWrite(c_pin_sdata1, level)
// #define set_pin_sclk(level) digitalWrite(c_pin_sclk, level)
// #define set_pin_rclk(level) digitalWrite(c_pin_rclk, level)

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
    IDX_UNIT_4 = 0,
    IDX_UNIT_3 = 13,
    IDX_UNIT_2,
    IDX_UNIT_1,
    IDX_UNIT_0,

    IDX_VOLUME_5 = 1,
    IDX_VOLUME_4,
    IDX_VOLUME_3,
    IDX_VOLUME_2,
    IDX_VOLUME_1,
    IDX_VOLUME_0,

    IDX_TOTAL_5 = 7,
    IDX_TOTAL_4,
    IDX_TOTAL_3,
    IDX_TOTAL_2,
    IDX_TOTAL_1,
    IDX_TOTAL_0,

    IDX_SELECT_L = IDX_TOTAL_5,
    IDX_SELECT_P = IDX_SELECT_L,

} byte_index_t;

typedef enum
{
    LL_IDX_LL_1 = IDX_TOTAL_4,
    LL_IDX_LL_2 = IDX_TOTAL_5,

    LL_IDX_TOT_LITERS_9 = IDX_TOTAL_3,
    LL_IDX_TOT_LITERS_8,
    LL_IDX_TOT_LITERS_7,
    LL_IDX_TOT_LITERS_6,

    LL_IDX_TOT_LITERS_5 = IDX_VOLUME_5,
    LL_IDX_TOT_LITERS_4,
    LL_IDX_TOT_LITERS_3,
    LL_IDX_TOT_LITERS_2,
    LL_IDX_TOT_LITERS_1,
    LL_IDX_TOT_LITERS_0,

} ll_byte_index_t;

typedef union
{
    struct
    {
        uint8_t IDX : 4; // first 4 bits
        uint8_t DIG : 4; // last 4 bits
    };
    uint8_t u8int;
} byte_t;

typedef struct
{
    union
    {
        struct
        {
            uint8_t IDX : 4; // first 4 bits
            uint8_t DIG : 4; // last 4 bits
        };
        uint8_t u8int;
    } data;
    struct
    {
        uint8_t idx : 7;
        uint8_t level : 1;
        
        uint32_t delay;
    } bit_delay;
    uint32_t pakt_delay;
} data_t;

#define BIT_HIGH_DELAY_US 20
#define BIT_LOW_DELAY_US 8
#define BIT_ADD_DELAY_US 182 // = 242us
// 8 for no clock delay bit delay
data_t sdata1_tx[PACKET_SIZE] = {
    {.data = {.u8int = 0x20}, .bit_delay = {.idx = 5, .level = 1, .delay = 157}, .pakt_delay = 0},   // U4
    {.data = {.u8int = 0xFF}, .bit_delay = {.idx = 8, .level = 0, .delay =   0}, .pakt_delay = 442}, // L5, 1.048
    {.data = {.u8int = 0xFE}, .bit_delay = {.idx = 8, .level = 0, .delay =   0}, .pakt_delay = 268},
    {.data = {.u8int = 0x0D}, .bit_delay = {.idx = 8, .level = 0, .delay =   0}, .pakt_delay = 181},
    {.data = {.u8int = 0x2C}, .bit_delay = {.idx = 1, .level = 0, .delay = 231}, .pakt_delay = 46},
    {.data = {.u8int = 0x6B}, .bit_delay = {.idx = 0, .level = 1, .delay = 164}, .pakt_delay = 42},
    {.data = {.u8int = 0x0A}, .bit_delay = {.idx = 8, .level = 0, .delay = 182}, .pakt_delay = 265}, // L0
    {.data = {.u8int = 0xF9}, .bit_delay = {.idx = 8, .level = 0, .delay = 143}, .pakt_delay = 181}, // T5, 300.00
    {.data = {.u8int = 0xF8}, .bit_delay = {.idx = 1, .level = 1, .delay = 145}, .pakt_delay = 49},
    {.data = {.u8int = 0x77}, .bit_delay = {.idx = 1, .level = 0, .delay = 146}, .pakt_delay = 44},
    {.data = {.u8int = 0x46}, .bit_delay = {.idx = 3, .level = 0, .delay = 229}, .pakt_delay = 46},
    {.data = {.u8int = 0x35}, .bit_delay = {.idx = 3, .level = 0, .delay = 146}, .pakt_delay = 42},
    {.data = {.u8int = 0x64}, .bit_delay = {.idx = 5, .level = 0, .delay = 229}, .pakt_delay = 46}, // T0
    {.data = {.u8int = 0x83}, .bit_delay = {.idx = 5, .level = 0, .delay = 146}, .pakt_delay = 43}, // U3, 286.00
    {.data = {.u8int = 0x62}, .bit_delay = {.idx = 7, .level = 1, .delay = 140}, .pakt_delay = 45},
    {.data = {.u8int = 0x01}, .bit_delay = {.idx = 7, .level = 1, .delay = 163}, .pakt_delay = 41},
    {.data = {.u8int = 0x00}, .bit_delay = {.idx = 6, .level = 1, .delay = 182}, .pakt_delay = 45}, // U0
};

const uint8_t price_idx_map[] = {
    IDX_UNIT_0,
    IDX_UNIT_1,
    IDX_UNIT_2,
    IDX_UNIT_3,
    IDX_UNIT_4,
};
const uint8_t volume_idx_map[] = {
    IDX_VOLUME_0,
    IDX_VOLUME_1,
    IDX_VOLUME_2,
    IDX_VOLUME_3,
    IDX_VOLUME_4,
    IDX_VOLUME_5,
};
const uint8_t total_idx_map[] = {
    IDX_TOTAL_0,
    IDX_TOTAL_1,
    IDX_TOTAL_2,
    IDX_TOTAL_3,
    IDX_TOTAL_4,
    IDX_TOTAL_5,
};

const size_t repeat_packet = 20;

const float volume_l_step = 1.0;                // 0.001
const float unit_price = 313.25;                // 0.01
const uint64_t totalizer_value = 1234567891ULL; // 0.001, totalizer has 3 decimal points

float volume_l = 0; // 0.001

void send_byte(const data_t *data, const bool en_rclk);
void send_packet();

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

void create_buffer_sanki_6(float volume, float price)
{
    uint32_t unit_001 = (uint32_t)(price * 100.0L);           // 0.01
    uint32_t total_001 = (uint32_t)(volume * price * 100.0L); // 0.01
    uint32_t volume_0001 = (uint32_t)(volume * 1000.0L);      // 0.001

    Serial.println("VOL: " + String(volume_0001 / 1000.0, 3) + "\t UNIT: " + String(unit_001 / 100.0, 2) + "\t Total : " + String(total_001 / 100.0, 2));

    // load unit price 0.01
    for (size_t i = 0; i < sizeof(price_idx_map); i++)
    {
        const uint8_t idx = price_idx_map[i];
        if (unit_001)
        {
            sdata1_tx[idx].data.DIG = unit_001 % 10;
            unit_001 /= 10;
        }
        else if (i == 0)
        {
            sdata1_tx[idx].data.DIG = 0x0;
        }
        else
        {
            sdata1_tx[idx].data.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load total price 0.01
    for (size_t i = 0; i < sizeof(total_idx_map); i++)
    {
        const uint8_t idx = total_idx_map[i];
        if (total_001)
        {
            sdata1_tx[idx].data.DIG = total_001 % 10;
            total_001 /= 10;
        }
        else if (i == 0)
        {
            sdata1_tx[idx].data.DIG = 0x0;
        }
        else
        {
            sdata1_tx[idx].data.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load liters 0.001
    for (size_t i = 0; i < sizeof(volume_idx_map); i++)
    {
        const uint8_t idx = volume_idx_map[i];
        if (volume_0001)
        {
            sdata1_tx[idx].data.DIG = volume_0001 % 10;
            volume_0001 /= 10;
        }
        else if (i == 0)
        {
            sdata1_tx[idx].data.DIG = 0x0;
        }
        else
        {
            sdata1_tx[idx].data.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }
}

void create_buffer_sunki_6_ll(uint64_t total_ll, float price)
{
    uint32_t unit_001 = (uint32_t)(price * 100.0L); // 0.01
    uint64_t total_0001 = (uint64_t)(total_ll);
    char str[100] = {};
    Serial.println("LL: " + String(uintToStr(total_0001, str)));

    // set Totaliser detect charactors
    sdata1_tx[LL_IDX_LL_1].data.DIG = DIS_CHARACTOR_L;
    sdata1_tx[LL_IDX_LL_2].data.DIG = DIS_CHARACTOR_L;

    // load unit price 0.01
    for (size_t i = 0; i < sizeof(price_idx_map); i++)
    {
        const uint8_t idx = price_idx_map[i];
        if (unit_001)
        {
            sdata1_tx[idx].data.DIG = unit_001 % 10;
            unit_001 /= 10;
        }
        else if (i == 0)
        {
            sdata1_tx[idx].data.DIG = 0x0;
        }
        else
        {
            sdata1_tx[idx].data.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load digits for liters section 0.001
    for (size_t i = 0; i < sizeof(volume_idx_map); i++)
    {
        const uint8_t idx = volume_idx_map[i];
        if (total_0001)
        {
            sdata1_tx[idx].data.DIG = total_0001 % 10;
            total_0001 /= 10;
        }
        else
        {
            sdata1_tx[idx].data.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
        }
    }

    // load digits for total section
    for (size_t i = 0; i < (sizeof(total_idx_map) - 3); i++)
    {
        const uint8_t idx = total_idx_map[i];
        if (total_0001)
        {
            sdata1_tx[idx].data.DIG = total_0001 % 10;
            total_0001 /= 10;
        }
        else
        {
            sdata1_tx[idx].data.DIG = DIS_CHARACTOR_BLANK; // blank for the rest
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
    set_pin_sclk(false);
    set_pin_rclk(false);

    Serial.begin(115200);
}

void loop()
{
    while (!digitalRead(c_pin_button))
        ; // waiting for release
    while (!digitalRead(c_pin_button_ll))
        ; // waiting for release

    Serial.println("Waiting for button A0 - Pump | A1 - Totaliser");

    while (1)
    {
        if (!digitalRead(c_pin_button))
        {
            delay(100);
            while (!digitalRead(c_pin_button))
                ; // waiting for release
            goto pump;
        }
        if (!digitalRead(c_pin_button_ll))
        {
            delay(100);
            while (!digitalRead(c_pin_button_ll))
                ; // waiting for release
            goto totaliser;
        }
    }

pump:
    volume_l = 0;

    while (1)
    {
        create_buffer_sanki_6(volume_l, unit_price);
        volume_l += volume_l_step;
        // repeating packet for a row
        for (size_t j = 0; j < repeat_packet; j++)
        {
            digitalWrite(c_pin_green_led, HIGH);
            send_packet();
            digitalWrite(c_pin_green_led, LOW);
            delayMicroseconds(25000);
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
    create_buffer_sunki_6_ll(totalizer_value, unit_price);
    while (1)
    {
        digitalWrite(c_pin_green_led, HIGH);
        send_packet();
        digitalWrite(c_pin_green_led, LOW);
        delayMicroseconds(153);

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
end:
    Serial.println("Send data Done");
}

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

void send_packet()
{
    // send first byte
    send_byte(&sdata1_tx[0], false);
    // send rest of the bytes
    for (size_t i = 1; i < PACKET_SIZE; i++)
    {
        send_byte(sdata1_tx + i, true); // send byte with rclk signal enable
    }
}

// delay_bit_num - 0 ~ 7. 8 for no delay
void send_byte(const data_t *data, const bool en_rclk)
{
    const uint8_t bit_map[] = {
        0b10000000,
        0b01000000,
        0b00100000,
        0b00010000,
        0b00001000,
        0b00000100,
        0b00000010,
        0b00000001};
    delayMicroseconds(data->pakt_delay);
    for (int i = 0; i < 8; i++)
    {
        const bool bit = (bool)(data->data.u8int & bit_map[i]);
        set_pin_sclk(false);
        set_pin_sdata1(bit);
        if (data->bit_delay.idx == i)
        {
            if (data->bit_delay.level)
            {
                delayMicroseconds(BIT_LOW_DELAY_US);
                set_pin_sclk(true);
                delayMicroseconds(data->bit_delay.delay);
            }
            else
            {
                delayMicroseconds(data->bit_delay.delay);
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
    if (en_rclk) // at the last bit, toggle rclk
    {
        set_pin_rclk(true);
        delayMicroseconds(8);
        set_pin_rclk(false);
    }
}