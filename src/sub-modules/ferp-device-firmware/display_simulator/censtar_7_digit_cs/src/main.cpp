#include <Arduino.h>

#define PACKET_SIZE 19

#if defined(ARDUINO_SAM_DUE)
    #define set_pin_rclk(level) level ? PIOC->PIO_SODR = PIO_PC26 : PIOC->PIO_CODR = PIO_PC26;
    #define set_pin_sclk(level) level ? PIOC->PIO_SODR = PIO_PC24 : PIOC->PIO_CODR = PIO_PC24;
    #define set_pin_sdata1(level) level ? PIOC->PIO_SODR = PIO_PC28 : PIOC->PIO_CODR = PIO_PC28;
    // #define set_pin_sdata2(level) level ? PIOB->PIO_SODR = PIO_PB25 : PIOB->PIO_CODR = PIO_PB25;
#else
    #define set_pin_sdata1(level) digitalWrite(c_pin_sdata1, level)
    #define set_pin_sclk(level) digitalWrite(c_pin_sclk, level)
    #define set_pin_rclk(level) digitalWrite(c_pin_rclk, level)
#endif

#if defined(ESP32)
const char c_pin_sdata1 = 27;
const char c_pin_sdata2 = 26;
const char c_pin_sclk = 14;
const char c_pin_rclk = 12;

const char c_pin_red_led = 2;
const char c_pin_green_led = 4;
const uint32_t c_pin_button = 0;    // pump start signal
const uint32_t c_pin_button_ll = 15; // LL totalizer send signal
#else
const char c_pin_sdata1 = 3; // PC28
const char c_pin_sdata2 = 2; // PB25
const char c_pin_sclk = 6;   // PC24
const char c_pin_rclk = 4;   // PC26

const char c_pin_red_led = 10;
const char c_pin_green_led = 9;
const uint32_t c_pin_button = A0;    // pump start signal
const uint32_t c_pin_button_ll = A1; // LL totalizer send signal
#endif

typedef enum
{
	CH_0 = 0,
	CH_1,
	CH_2,
	CH_3,
	CH_4,
	CH_5,
	CH_6,
	CH_7,
	CH_8,
	CH_9,
	CH_L,
	CH_H,
	CH_P,
	CH_A,
	CH_DA,
	CH_BL,
} display_charactor_t;

typedef enum
{
    IDX_UNIT_0 = 0,
    IDX_UNIT_1,
    IDX_UNIT_2,
    IDX_UNIT_3,
    IDX_UNIT_4,
    IDX_UNIT_SIZE,

    IDX_TOTAL_0 = 5,
    IDX_TOTAL_1,
    IDX_TOTAL_2,
    IDX_TOTAL_3,
    IDX_TOTAL_4,
    IDX_TOTAL_5,
    IDX_TOTAL_6,
    IDX_TOTAL_SIZE,

    IDX_LITERS_0 = 12,
    IDX_LITERS_1,
    IDX_LITERS_2,
    IDX_LITERS_3,
    IDX_LITERS_4,
    IDX_LITERS_5,
    IDX_LITERS_6,
    IDX_LITERS_SIZE,

    IDX_SELECT_L = IDX_TOTAL_6,
    IDX_SELECT_P = IDX_TOTAL_6
} byte_index_t;

typedef enum
{
	LL_IDX_LL_1 = IDX_TOTAL_5,
	LL_IDX_LL_2 = IDX_TOTAL_6,

	LL_IDX_TOT_LITERS_0  = IDX_LITERS_0,
	LL_IDX_TOT_LITERS_1  = IDX_LITERS_1,
	LL_IDX_TOT_LITERS_2  = IDX_LITERS_2,
	LL_IDX_TOT_LITERS_3  = IDX_LITERS_3,
	LL_IDX_TOT_LITERS_4  = IDX_LITERS_4,
	LL_IDX_TOT_LITERS_5  = IDX_LITERS_5,
	LL_IDX_TOT_LITERS_6  = IDX_LITERS_6,
	LL_IDX_TOT_LITERS_7  = IDX_TOTAL_0,
	LL_IDX_TOT_LITERS_8  = IDX_TOTAL_1,
	LL_IDX_TOT_LITERS_9  = IDX_TOTAL_2,
	LL_IDX_TOT_LITERS_10 = IDX_TOTAL_3,
	LL_IDX_TOT_LITERS_11 = IDX_TOTAL_4,
} ll_byte_intex_t;

typedef union
{
    struct
    {
        uint16_t INDX : 8; // first 8 bits, indexing
        uint16_t DIGI : 4; // last 4 bits, segment display number
        uint16_t : 4;      // rest 4 bits, no use in sending
    };
    uint16_t w_data;
} data_t;

data_t sdata1_tx[PACKET_SIZE] = {
    {{.INDX = 0x45, .DIGI = CH_0}},   // Price IDX 0
    {{.INDX = 0x35, .DIGI = CH_0}},   // Price IDX 1
    {{.INDX = 0x25, .DIGI = CH_5}},   // Price IDX 2
    {{.INDX = 0x15, .DIGI = CH_0}},   // Price IDX 3
    {{.INDX = 0x05, .DIGI = CH_3}},   // Price IDX 4

    {{.INDX = 0xB5, .DIGI = CH_0}},   // Total IDX 0
    {{.INDX = 0xA5, .DIGI = CH_0}},   // Total IDX 1
    {{.INDX = 0x95, .DIGI = CH_0}},   // Total IDX 2
    {{.INDX = 0x85, .DIGI = CH_5}},   // Total IDX 3
    {{.INDX = 0x75, .DIGI = CH_3}},   // Total IDX 4
    {{.INDX = 0x65, .DIGI = CH_BL}},  // Total IDX 5
    {{.INDX = 0x55, .DIGI = CH_BL}},  // Total IDX 6

    {{.INDX = 0xF2, .DIGI = CH_8}},   // Volume IDX 0
    {{.INDX = 0xF1, .DIGI = CH_4}},   // Volume IDX 1
    {{.INDX = 0xF0, .DIGI = CH_1}},   // Volume IDX 2
    {{.INDX = 0xF3, .DIGI = CH_1}},   // Volume IDX 3
    {{.INDX = 0xE5, .DIGI = CH_BL}},  // Volume IDX 4
    {{.INDX = 0xD5, .DIGI = CH_BL}},  // Volume IDX 5
    {{.INDX = 0xC5, .DIGI = CH_BL}},  // Volume IDX 6
};


const size_t repeat_packet = 10; //100

const float volume_l_step = 2.0; // 0.001
const float unit_price = 305.00; // 0.01
const uint64_t totalizer_value = 123456789123ULL; //0.001, totalizer has 3 digits

float volume_l = 0; // 0.001

void delayNanoseconds(uint32_t nsec);
void send_packet();
void send_12bits(uint16_t bits);
void send_16bits(uint16_t bits);
void send_20bits(uint32_t bits);

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

void create_buffer_censtar_7(float volume, float price)
{
    uint32_t unit_001 = (uint32_t)(price * 100);           // 0.01
    uint32_t total_001 = (uint32_t)(volume * price * 100.0L); // 0.01
    uint32_t volume_0001 = (uint32_t)(volume * 1000);      // 0.001

    Serial.println("VOL: " + String(volume_0001 / 1000.0, 3) + "\t UNIT: " + String(unit_001 / 100.0, 2) + "\t Total : " + String(total_001 / 100.0, 2));

    // load unit price 0.01
    for (size_t i = IDX_UNIT_0; i < IDX_UNIT_SIZE; i++)
    {
        if(unit_001)
        {
            sdata1_tx[i].DIGI = unit_001%10;
            unit_001 /= 10;
        }
        else if(i == IDX_UNIT_0)
        {
            sdata1_tx[i].DIGI = 0x0;
        }
        else
        {
            sdata1_tx[i].DIGI = CH_BL; // blank for the rest
        }
    }
    
    // load total price 0.01
    for (size_t i = IDX_TOTAL_0; i < IDX_TOTAL_SIZE; i++)
    {
        if(total_001)
        {
            sdata1_tx[i].DIGI = total_001%10;
            total_001 /= 10;
        }
        else if(i == IDX_TOTAL_0)
        {
            sdata1_tx[i].DIGI = 0x0;
        }
        else
        {
            sdata1_tx[i].DIGI = CH_BL; // blank for the rest
        }
    }
    
    // load liters 0.001
    for (size_t i = IDX_LITERS_0; i < IDX_LITERS_SIZE; i++)
    {
        if(volume_0001)
        {
            sdata1_tx[i].DIGI = volume_0001%10;
            volume_0001 /= 10;
        }
        else if(i == IDX_LITERS_0)
        {
            sdata1_tx[i].DIGI = 0x0;
        }
        else
        {
            sdata1_tx[i].DIGI = CH_BL; // blank for the rest
        }
    }
}

void create_buffer_censtar_7_ll(uint64_t total_ll, float price)
{
    uint32_t unit_001 = (uint32_t)(price * 100);           // 0.01
    uint64_t total_0001 = (uint64_t)(total_ll);
    char str[100] = {};
    Serial.println("LL: " + String(uintToStr(total_0001, str)));

    //set Totaliser detect charactors
    sdata1_tx[LL_IDX_LL_1].DIGI = CH_L;
    sdata1_tx[LL_IDX_LL_2].DIGI = CH_L;

    // load unit price 0.01
    for (size_t i = IDX_UNIT_0; i < IDX_UNIT_SIZE; i++)
    {
        if(unit_001)
        {
            sdata1_tx[i].DIGI = unit_001%10;
            unit_001 /= 10;
        }
        else if(i == IDX_UNIT_0)
        {
            sdata1_tx[i].DIGI = 0x0;
        }
        else
        {
            sdata1_tx[i].DIGI = CH_BL; // blank for the rest
        }
    }

    //load digits for liters section 0.001
    for (size_t i = IDX_LITERS_0; i <= IDX_LITERS_6; i++)
    {
        if (total_0001)
        {
            sdata1_tx[i].DIGI = total_0001 % 10;
            total_0001 /= 10;
        }
        else
        {
            sdata1_tx[i].DIGI = CH_BL; // blank for the rest
        }
    }

    // load digits for total section
    for (size_t i = IDX_TOTAL_0; i <= IDX_TOTAL_4; i++)
    {
        if (total_0001)
        {
            sdata1_tx[i].DIGI = total_0001 % 10;
            total_0001 /= 10;
        }
        else
        {
            sdata1_tx[i].DIGI = CH_BL; // blank for the rest
        }
    }
}

void setup()
{
    pinMode(c_pin_sdata2, OUTPUT);
    pinMode(c_pin_sdata1, OUTPUT);
    pinMode(c_pin_sclk, OUTPUT);
    pinMode(c_pin_rclk, OUTPUT);
    pinMode(c_pin_red_led, OUTPUT);
    pinMode(c_pin_green_led, OUTPUT);
    pinMode(c_pin_button, INPUT_PULLUP);
    pinMode(c_pin_button_ll, INPUT_PULLUP);
    digitalWrite(c_pin_green_led, LOW);

    set_pin_sdata1(false);
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
        create_buffer_censtar_7(volume_l, unit_price);
        volume_l += volume_l_step;

        // repeating packet for a row
        for (size_t j = 0; j < repeat_packet; j++)
        {
            digitalWrite(c_pin_green_led, HIGH);
            send_packet();
            digitalWrite(c_pin_green_led, LOW);
            delay(59);
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
        send_packet();
        digitalWrite(c_pin_green_led, LOW);
        delay(40);
        
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
#define RCLK_HIGH_NS 1800
#define RCLK_LOW_NS 1000

    for (size_t k = 0; k < PACKET_SIZE; k++)
    {
        const data_t data = sdata1_tx[k];
        send_12bits(data.w_data);
        set_pin_rclk(true);
        delayNanoseconds(RCLK_HIGH_NS);
        set_pin_rclk(false);
        delayNanoseconds(RCLK_LOW_NS);
    }

    //0x2FF6
    send_16bits(0x2FF6);
    set_pin_rclk(true);
    delayNanoseconds(RCLK_HIGH_NS);
    set_pin_rclk(false);
    delayNanoseconds(RCLK_LOW_NS);

    //0x24FF6
    send_20bits(0x24FF6);
    set_pin_rclk(true);
    delayNanoseconds(RCLK_HIGH_NS);
    set_pin_rclk(false);
    delayNanoseconds(RCLK_LOW_NS);

    //send rest of fixed bytes
    send_12bits(0x045);
    send_12bits(0x01A);
    send_12bits(0xC4C);
    send_12bits(0xBE2);
    send_12bits(0xBE0);
    send_12bits(0xA1B);
    send_12bits(0x506);
    send_12bits(0x942);
    send_12bits(0x5BF);
    send_12bits(0x85F);
    send_12bits(0xBD7);
    send_12bits(0xD9B);
    send_12bits(0xEAA);
    send_12bits(0x1E4);
    send_12bits(0x0F1);
    send_12bits(0x0F0);
    send_12bits(0x0F3);
    send_12bits(0xFE6);
    send_12bits(0xFEA);
    send_12bits(0xFE1);
    send_12bits(0x40B);
    send_12bits(0xFEC);
    send_12bits(0x64F);

}

void send_12bits(uint16_t bits)
{
#if defined(ESP32)
    #define BIT12_DATA_POST_NS 180
    #define BIT12_CLK_LOW_NS 950
    #define BIT12_CLK_HIGH_NS 900
    #define BIT12_CLK_HIGH_LAST_NS 1100
#else
    #define BIT12_DATA_POST_NS 280
    #define BIT12_CLK_LOW_NS 1250
    #define BIT12_CLK_HIGH_NS 1000
    #define BIT12_CLK_HIGH_LAST_NS 1200
#endif

    const uint16_t bit_map[] = {
        0b0000100000000000,
        0b0000010000000000,
        0b0000001000000000,
        0b0000000100000000,
        0b0000000010000000,
        0b0000000001000000,
        0b0000000000100000,
        0b0000000000010000,
        0b0000000000001000,
        0b0000000000000100,
        0b0000000000000010,
        0b0000000000000001};
    {
        const bool bit = (bool)(bits & bit_map[0]);
        set_pin_sclk(false);
        delayNanoseconds(BIT12_DATA_POST_NS);
        set_pin_sdata1(bit);
        delayNanoseconds(BIT12_CLK_LOW_NS);
        set_pin_sclk(true);
        delayNanoseconds(BIT12_CLK_HIGH_NS);
    }
    for (int i = 1; i < 11; i++)
    {
        const bool bit = (bool)(bits & bit_map[i]);
        set_pin_sdata1(bit);
        delayNanoseconds(BIT12_DATA_POST_NS);
        set_pin_sclk(false);
        delayNanoseconds(BIT12_CLK_LOW_NS);
        set_pin_sclk(true);
        delayNanoseconds(BIT12_CLK_HIGH_NS);
    }
    // send last bit with different delays
    {
        const bool bit = (bool)(bits & bit_map[11]);
        set_pin_sdata1(bit);
        delayNanoseconds(BIT12_DATA_POST_NS);
        set_pin_sclk(false);
        delayNanoseconds(BIT12_CLK_LOW_NS);
        set_pin_sclk(true);
        delayNanoseconds(BIT12_CLK_HIGH_LAST_NS);
    }
}

void send_16bits(uint16_t bits)
{
#define BIT16_DATA_POST_NS 300
#define BIT16_CLK_LOW_NS 1250
#define BIT16_CLK_HIGH_NS 1000
#define BIT16_CLK_HIGH_LAST_NS 1200

    const uint16_t bit_map[] = {
        0b1000000000000000,
        0b0100000000000000,
        0b0010000000000000,
        0b0001000000000000,
        0b0000100000000000,
        0b0000010000000000,
        0b0000001000000000,
        0b0000000100000000,
        0b0000000010000000,
        0b0000000001000000,
        0b0000000000100000,
        0b0000000000010000,
        0b0000000000001000,
        0b0000000000000100,
        0b0000000000000010,
        0b0000000000000001};
    for (int i = 0; i < 15; i++)
    {
        const bool bit = (bool)(bits & bit_map[i]);
        set_pin_sdata1(bit);
        delayNanoseconds(BIT16_DATA_POST_NS);
        set_pin_sclk(false);
        delayNanoseconds(BIT16_CLK_LOW_NS);
        set_pin_sclk(true);
        delayNanoseconds(BIT16_CLK_HIGH_NS);
    }
    // send last bit with different delays
    {
        const bool bit = (bool)(bits & bit_map[15]);
        set_pin_sdata1(bit);
        delayNanoseconds(BIT16_DATA_POST_NS);
        set_pin_sclk(false);
        delayNanoseconds(BIT16_CLK_LOW_NS);
        set_pin_sclk(true);
        delayNanoseconds(BIT16_CLK_HIGH_LAST_NS);
    }
}
void send_20bits(uint32_t bits)
{
#define BIT20_DATA_POST_NS 300
#define BIT20_CLK_LOW_NS 1250
#define BIT20_CLK_HIGH_NS 1000
#define BIT20_CLK_HIGH_LAST_NS 1200

    const uint32_t bit_map[] = {
        0b10000000000000000000,
        0b01000000000000000000,
        0b00100000000000000000,
        0b00010000000000000000,
        0b00001000000000000000,
        0b00000100000000000000,
        0b00000010000000000000,
        0b00000001000000000000,
        0b00000000100000000000,
        0b00000000010000000000,
        0b00000000001000000000,
        0b00000000000100000000,
        0b00000000000010000000,
        0b00000000000001000000,
        0b00000000000000100000,
        0b00000000000000010000,
        0b00000000000000001000,
        0b00000000000000000100,
        0b00000000000000000010,
        0b00000000000000000001};
    for (int i = 0; i < 19; i++)
    {
        const bool bit = (bool)(bits & bit_map[i]);
        set_pin_sdata1(bit);
        delayNanoseconds(BIT20_DATA_POST_NS);
        set_pin_sclk(false);
        delayNanoseconds(BIT20_CLK_LOW_NS);
        set_pin_sclk(true);
        delayNanoseconds(BIT20_CLK_HIGH_NS);
    }
    // send last bit with different delays
    {
        const bool bit = (bool)(bits & bit_map[19]);
        set_pin_sdata1(bit);
        delayNanoseconds(BIT20_DATA_POST_NS);
        set_pin_sclk(false);
        delayNanoseconds(BIT20_CLK_LOW_NS);
        set_pin_sclk(true);
        delayNanoseconds(BIT20_CLK_HIGH_LAST_NS);
    }
}