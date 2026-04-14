#include <Arduino.h>

#define PACKET_SIZE 24

#if defined(ARDUINO_SAM_DUE)
    #define set_pin_rclk(level) level ? PIOC->PIO_SODR = PIO_PC26 : PIOC->PIO_CODR = PIO_PC26;
    #define set_pin_sclk(level) level ? PIOC->PIO_SODR = PIO_PC24 : PIOC->PIO_CODR = PIO_PC24;
    #define set_pin_sdata1(level) level ? PIOC->PIO_SODR = PIO_PC28 : PIOC->PIO_CODR = PIO_PC28;
    #define set_pin_sdata2(level) level ? PIOB->PIO_SODR = PIO_PB25 : PIOB->PIO_CODR = PIO_PB25;
#else
    #define set_pin_rclk(level) digitalWrite(c_pin_rclk, level)
    #define set_pin_sclk(level) digitalWrite(c_pin_sclk, level)
    #define set_pin_sdata1(level) digitalWrite(c_pin_sdata1, level)
    #define set_pin_sdata2(level) digitalWrite(c_pin_sdata2, level)
#endif

const char c_pin_sdata1 = 3; // PC28
const char c_pin_sdata2 = 2; // PB25
const char c_pin_sclk = 6;   // PC24
const char c_pin_rclk = 4;   // PC26

const uint32_t c_pin_red_led = 10;
const uint32_t c_pin_green_led = 9;
const uint32_t c_pin_button = A0;    // pump start signal
const uint32_t c_pin_button_ll = A1; // LL totalizer send signal

#define BL 0xA

#define CODE_DIGIT_MAP(XX) \
    XX(0, 0x3F, 0xBF, 0)   \
    XX(1, 0x06, 0x86, 1)   \
    XX(2, 0x5B, 0xDB, 2)   \
    XX(3, 0x4F, 0xCF, 3)   \
    XX(4, 0x66, 0xE6, 4)   \
    XX(5, 0x6D, 0xED, 5)   \
    XX(6, 0x7D, 0xFD, 6)   \
    XX(7, 0x47, 0xC7, 7)   \
    XX(8, 0x7F, 0xFF, 8)   \
    XX(9, 0x6F, 0xEF, 9)   \
    XX(BL, 0x00, 0x80, 0)

enum
{
    IDX_UNIT_0 = 0,
    IDX_UNIT_1,
    IDX_UNIT_2,
    IDX_UNIT_3,
    IDX_UNIT_4,
    IDX_UNIT_5,
    IDX_UNIT_6,
    IDX_UNIT_7,

    IDX_VOLUME_0,
    IDX_VOLUME_1,
    IDX_VOLUME_2,
    IDX_VOLUME_3,
    IDX_VOLUME_4,
    IDX_VOLUME_5,
    IDX_VOLUME_6,
    IDX_VOLUME_7,

    IDX_TOTAL_0,
    IDX_TOTAL_1,
    IDX_TOTAL_2,
    IDX_TOTAL_3,
    IDX_TOTAL_4,
    IDX_TOTAL_5,
    IDX_TOTAL_6,
    IDX_TOTAL_7,

    IDX_SELECT_L = IDX_TOTAL_5,
    IDX_SELECT_P = IDX_SELECT_L
};

enum
{
#define XX(CODE, DIGIT, DIGIT_DECI, VALUE) DIGI_##CODE = DIGIT,
    CODE_DIGIT_MAP(XX)
#undef XX
#define XX(CODE, DIGIT, DIGIT_DECI, VALUE) DIGI_DECI_##CODE = DIGIT_DECI,
        CODE_DIGIT_MAP(XX)
#undef XX
};

// This pump doesn't give totaliser on display, only show in keypad display

const uint8_t digit_map[] = {
#define XX(CODE, DIGIT, DIGIT_DECI, VALUE) [CODE] = DIGIT,
    CODE_DIGIT_MAP(XX)
#undef XX
};

const uint8_t digit_deci_map[] = {
#define XX(CODE, DIGIT, DIGIT_DECI, VALUE) [CODE] = DIGIT_DECI,
    CODE_DIGIT_MAP(XX)
#undef XX
};

const uint8_t price_idx_map[] = {
    IDX_UNIT_0,
    IDX_UNIT_1,
    IDX_UNIT_2,
    IDX_UNIT_3,
    IDX_UNIT_4,
    IDX_UNIT_5,
    IDX_UNIT_6,
    IDX_UNIT_7};
const uint8_t volume_idx_map[] = {
    IDX_VOLUME_0,
    IDX_VOLUME_1,
    IDX_VOLUME_2,
    IDX_VOLUME_3,
    IDX_VOLUME_4,
    IDX_VOLUME_5,
    IDX_VOLUME_6,
    IDX_VOLUME_7,
};
const uint8_t total_idx_map[] = {
    IDX_TOTAL_0,
    IDX_TOTAL_1,
    IDX_TOTAL_2,
    IDX_TOTAL_3,
    IDX_TOTAL_4,
    IDX_TOTAL_5,
    IDX_TOTAL_6,
    IDX_TOTAL_7,
};

uint8_t sdata1_tx[PACKET_SIZE] = {
    DIGI_1,      // DIGI_0,
    DIGI_2,      // DIGI_0,
    DIGI_DECI_3, // DIGI_DECI_9,
    DIGI_4,      // DIGI_0,
    DIGI_5,      // DIGI_3,
    DIGI_6,      // DIGI_BL,
    DIGI_7,      // DIGI_BL,
    DIGI_8,      // DIGI_BL, // unit price ---309.00

    DIGI_8,      // DIGI_0,
    DIGI_7,      // DIGI_5,
    DIGI_6,      // DIGI_2,
    DIGI_DECI_5, // DIGI_DECI_1,
    DIGI_4,      // DIGI_BL,
    DIGI_3,      // DIGI_BL,
    DIGI_2,      // DIGI_BL,
    DIGI_1,      // DIGI_BL, // volume ----1.250

    DIGI_1,      // DIGI_5,
    DIGI_2,      // DIGI_2,
    DIGI_DECI_3, // DIGI_DECI_6,
    DIGI_4,      // DIGI_8,
    DIGI_5,      // DIGI_3,
    DIGI_6,      // DIGI_BL,
    DIGI_7,      // DIGI_BL,
    DIGI_8,      // DIGI_BL, // total price ---386.25
};

const size_t repeat_packet = 200;

const float volume_l_step = 1.0;                // 0.001
const float unit_price = 313.00;                // 0.01
const uint64_t totalizer_value = 1234567891ULL; // 0.001, totalizer has 3 decimal points

float volume_l = 0; // 0.001

void send_byte(unsigned char bite1);
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

void create_buffer_longfeng_8(float volume, float price)
{
    uint32_t unit_001 = (uint32_t)(price * 100.0L);           // 0.01
    uint32_t total_001 = (uint32_t)(volume * price * 100.0L); // 0.01
    uint32_t volume_0001 = (uint32_t)(volume * 1000.0L);      // 0.001

    Serial.println("VOL: " + String(volume_0001 / 1000.0, 3) + "\t UNIT: " + String(unit_001 / 100.0, 2) + "\t Total : " + String(total_001 / 100.0, 2));

    // return;
    // load unit price 0.01
    for (size_t i = 0; i < sizeof(price_idx_map); i++)
    {
        const uint8_t idx = price_idx_map[i];
        if (unit_001)
        {
            if (idx == IDX_UNIT_2)
                sdata1_tx[idx] = digit_deci_map[unit_001 % 10];
            else
                sdata1_tx[idx] = digit_map[unit_001 % 10];
            unit_001 /= 10;
        }
        else if (i == 0)
        {
            if (idx == IDX_UNIT_2)
                sdata1_tx[idx] = DIGI_DECI_0;
            else
                sdata1_tx[idx] = DIGI_0;
        }
        else
        {
            sdata1_tx[idx] = DIGI_BL; // blank for the rest
        }
    }

    // load total price 0.01
    for (size_t i = 0; i < sizeof(total_idx_map); i++)
    {
        const uint8_t idx = total_idx_map[i];
        if (total_001)
        {
            if (idx == IDX_TOTAL_2)
                sdata1_tx[idx] = digit_deci_map[total_001 % 10];
            else
                sdata1_tx[idx] = digit_map[total_001 % 10];
            // sdata1_tx[idx] = total_001 % 10;
            total_001 /= 10;
        }
        else if (i == 0)
        {
            if (idx == IDX_TOTAL_2)
                sdata1_tx[idx] = DIGI_DECI_0;
            else
                sdata1_tx[idx] = DIGI_0;
        }
        else
        {
            sdata1_tx[idx] = DIGI_BL; // blank for the rest
        }
    }

    // load liters 0.001
    for (size_t i = 0; i < sizeof(volume_idx_map); i++)
    {
        const uint8_t idx = volume_idx_map[i];
        if (volume_0001)
        {
            if (idx == IDX_VOLUME_3)
                sdata1_tx[idx] = digit_deci_map[volume_0001 % 10];
            else
                sdata1_tx[idx] = digit_map[volume_0001 % 10];

            // sdata1_tx[idx] = volume_0001 % 10;
            volume_0001 /= 10;
        }
        else if (i == 0)
        {
            if (idx == IDX_VOLUME_3)
                sdata1_tx[idx] = DIGI_DECI_0;
            else
                sdata1_tx[idx] = DIGI_0;
        }
        else
        {
            sdata1_tx[idx] = DIGI_BL; // blank for the rest
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

    set_pin_sdata1(false);
    set_pin_sdata2(true); // not using
    set_pin_sclk(true);
    set_pin_rclk(true);
    // digitalWrite(c_pin_rclk, true);

    Serial.begin(115200);
    // Serial.println("Packet Size = " + String(PACKET_SIZE) + " Row Size = " + String(sizeof(sdata1_tx_buffer) / PACKET_SIZE));
}

void loop()
{
    while (!digitalRead(c_pin_button))
        ; // waiting for release

    Serial.println("Waiting for button A0 - Pump");

    // while (1)
    // {
    //     if (!digitalRead(c_pin_button))
    //     {
    //         delay(100);
    //         while (!digitalRead(c_pin_button))
    //             ; // waiting for release
    //         goto pump;
    //     }
    // }

pump:
    volume_l = 0;

    while (1)
    {
        create_buffer_longfeng_8(volume_l, unit_price);
        volume_l += volume_l_step;
        // repeating packet for a row
        for (size_t j = 0; j < repeat_packet; j++)
        {
            digitalWrite(c_pin_green_led, HIGH);
            send_packet();
            digitalWrite(c_pin_green_led, LOW);
            delayMicroseconds(4913);
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
end:
    Serial.println("Send data Done");
}

void delay_nops(uint32_t count)
{
    while (count--)
    {
        asm volatile("nop");
    }
}

void send_packet()
{
#define send_output_byte(data)           \
    {                                    \
        bit = (bool)(data & bit_map[0]); \
        set_pin_sdata1(bit);             \
        set_pin_sclk(false);             \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        set_pin_sclk(true);              \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
                                         \
        bit = (bool)(data & bit_map[1]); \
        set_pin_sdata1(bit);             \
        set_pin_sclk(false);             \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        set_pin_sclk(true);              \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
                                         \
        bit = (bool)(data & bit_map[2]); \
        set_pin_sdata1(bit);             \
        set_pin_sclk(false);             \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        set_pin_sclk(true);              \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
                                         \
        bit = (bool)(data & bit_map[3]); \
        set_pin_sdata1(bit);             \
        set_pin_sclk(false);             \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        set_pin_sclk(true);              \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
                                         \
        bit = (bool)(data & bit_map[4]); \
        set_pin_sdata1(bit);             \
        set_pin_sclk(false);             \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        set_pin_sclk(true);              \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
                                         \
        bit = (bool)(data & bit_map[5]); \
        set_pin_sdata1(bit);             \
        set_pin_sclk(false);             \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        set_pin_sclk(true);              \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
                                         \
        bit = (bool)(data & bit_map[6]); \
        set_pin_sdata1(bit);             \
        set_pin_sclk(false);             \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        set_pin_sclk(true);              \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
                                         \
        bit = (bool)(data & bit_map[7]); \
        set_pin_sdata1(bit);             \
        set_pin_sclk(false);             \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        set_pin_sclk(true);              \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
                                         \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
        asm("nop\n\t");                  \
    }
    const uint8_t bit_map[] = {
        0b10000000,
        0b01000000,
        0b00100000,
        0b00010000,
        0b00001000,
        0b00000100,
        0b00000010,
        0b00000001};
    bool bit;

    send_output_byte(sdata1_tx[0]);
    send_output_byte(sdata1_tx[1]);
    send_output_byte(sdata1_tx[2]);
    send_output_byte(sdata1_tx[3]);
    send_output_byte(sdata1_tx[4]);
    send_output_byte(sdata1_tx[5]);
    send_output_byte(sdata1_tx[6]);
    send_output_byte(sdata1_tx[7]);
    send_output_byte(sdata1_tx[8]);
    send_output_byte(sdata1_tx[9]);
    send_output_byte(sdata1_tx[10]);
    send_output_byte(sdata1_tx[11]);
    send_output_byte(sdata1_tx[12]);
    send_output_byte(sdata1_tx[13]);
    send_output_byte(sdata1_tx[14]);
    send_output_byte(sdata1_tx[15]);
    send_output_byte(sdata1_tx[16]);
    send_output_byte(sdata1_tx[17]);
    send_output_byte(sdata1_tx[18]);
    send_output_byte(sdata1_tx[19]);
    send_output_byte(sdata1_tx[20]);
    send_output_byte(sdata1_tx[21]);
    send_output_byte(sdata1_tx[22]);
    send_output_byte(sdata1_tx[23]);

    set_pin_rclk(false);
    asm("nop\n\t");
    asm("nop\n\t");
    asm("nop\n\t");
    asm("nop\n\t");
    asm("nop\n\t");
    asm("nop\n\t");
    set_pin_rclk(true);
}

void send_byte(const uint8_t byte)
{
    for (int i = 0; i < 8; i++)
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
        const bool bit = (bool)(byte & bit_map[i]);
        set_pin_sclk(false);
        set_pin_sdata1(bit);
        set_pin_sclk(true);
        // asm("nop\n\t");
        // asm("nop\n\t");
        // asm("nop\n\t");
        // asm("nop\n\t");
        // asm("nop\n\t");
        // asm("nop\n\t");
        // asm("nop\n\t");
        // asm("nop\n\t");
        // asm("nop\n\t");
    }
}