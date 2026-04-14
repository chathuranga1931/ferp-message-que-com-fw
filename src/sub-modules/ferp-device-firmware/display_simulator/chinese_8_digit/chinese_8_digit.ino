
// #include <SoftwareSerial.h>

const char c_pin_sdata1 = 3;
const char c_pin_sdata2 = 2;
const char c_pin_sclk = 6;
const char c_pin_rclk = 4;

const char c_pin_rclk_other = 7; /*This pin is not used*/
const char c_pin_5v = 5;

const char c_pin_serial_tx = A4;
const char c_pin_serial_rx = A5;
const char c_pin_button = A0;
const char c_pin_button_Tester = A1;

const char c_pin_red_led = 13;
const char c_pin_green_led = 12;

const char repeat_same_display_packet_for_times = 1;

char only_emulate = 1;
// SoftwareSerial mySerial(c_pin_serial_rx, c_pin_serial_tx); // RX, TX

uint8_t channel1_tx_buffer[14] = {};
uint8_t channel2_tx_buffer[14] = {};

int count_digits(uint32_t num)
{
    int count = 0;

    if (num == 0)
    {
        return 1; // Special case: 0 has one digit
    }

    while (num != 0)
    {
        count++;
        num /= 10;
    }

    return count;
}

void create_buffer(uint32_t mili_volume, uint32_t unit_pricex100, bool start_stop)
{

    uint32_t total = (uint32_t)(mili_volume / 1000.0 * unit_pricex100 * 1.0);

    Serial.println("VOL: " + String(mili_volume / 1000.0) + "\t UNIT: " + String(unit_pricex100 / 100.0) + "\t Total : " + String(total / 100.00));

    uint8_t total_in_array[8];
    uint8_t unit_price_in_array[6];
    uint8_t volume_in_array[8];

    for (int i = 0; i < 7; i++)
    {
        total_in_array[i] = 0x0F;
        volume_in_array[i] = 0x0F;
    }
    for (int i = 0; i < 5; i++)
    {
        unit_price_in_array[i] = 0x0F;
    }

    uint32_t value = total;
    int correction = total / 1000 + count_digits(total) < 3 ? 3 : count_digits(total);
    for (int i = 7; i > 7 - correction; i--)
    {
        total_in_array[i] = value % 10;
        value /= 10;
    }

    value = unit_pricex100;
    correction = unit_pricex100 / 1000 + count_digits(unit_pricex100) < 3 ? 3 : count_digits(unit_pricex100);
    for (int i = 5; i > 5 - correction; i--)
    {
        unit_price_in_array[i] = value % 10;
        value /= 10;
    }

    value = mili_volume;
    correction = mili_volume / 1000 + count_digits(mili_volume) < 4 ? 4 : count_digits(mili_volume);
    for (int i = 7; i > 7 - correction; i--)
    {
        volume_in_array[i] = value % 10;
        value /= 10;
    }

    // // Serial.print the resulting array
    // for (int i = 0; i < 8; i++) {
    //     Serial.print(total_in_array[i], HEX);
    // }
    // Serial.print("\n");
    // // Print the resulting array
    // for (int i = 0; i < 6; i++) {
    //     Serial.print(unit_price_in_array[i], HEX);
    // }
    // Serial.print("\n");
    // // Print the resulting array
    // for (int i = 0; i < 8; i++) {
    //     Serial.print(volume_in_array[i], HEX);
    // }
    // Serial.print("\n");

    uint32_t idx = 0;
    uint32_t sub_idx = 0;
    channel1_tx_buffer[idx++] = ((total_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((total_in_array[sub_idx++] << 4) | (idx & 0x0F));
    sub_idx = 0;
    channel1_tx_buffer[idx++] = ((unit_price_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((unit_price_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((unit_price_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((unit_price_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((unit_price_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel1_tx_buffer[idx++] = ((unit_price_in_array[sub_idx++] << 4) | (idx & 0x0F));
    idx = 0;
    sub_idx = 0;
    channel2_tx_buffer[idx++] = ((volume_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((volume_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((volume_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((volume_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((volume_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((volume_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((volume_in_array[sub_idx++] << 4) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((volume_in_array[sub_idx++] << 4) | (idx & 0x0F));
    if (start_stop)
    {
        channel2_tx_buffer[idx++] = ((1 << 4) | (idx & 0x0F));
    }
    else
    {
        channel2_tx_buffer[idx++] = ((0 << 4) | (idx & 0x0F));
    }

    channel2_tx_buffer[idx++] = ((0xF0) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((0xF0) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((0xF0) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((0xF0) | (idx & 0x0F));
    channel2_tx_buffer[idx++] = ((0xF0) | (idx & 0x0F));

    // for (int i = 0; i < 14; i++) {
    //     Serial.print(channel1_tx_buffer[i], HEX);
    // }
    // Serial.print("\n");
    // for (int i = 0; i < 14; i++) {
    //     Serial.print(channel2_tx_buffer[i], HEX);
    // }
    // Serial.print("\n");
}

void nop()
{
    static volatile int i;
    i++;
}

void send_byte(unsigned char bite1, unsigned char bite2)
{
    for (int i = 0; i < 8; i++)
    {
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
    // mySerial.begin(9600);
    // Serial1.begin(9600);

    Serial.println("Buffer Size = " + String(sizeof(channel1_tx_buffer)));

    error = 0;
}

const unsigned char waiting_for_button_press = 0;
const unsigned char sending = 1;
const unsigned char waiting_for_7ms = 2;
const unsigned char waiting_for_900ms = 3;
const unsigned char verify_data = 4;

static unsigned char device_status = 0;
static unsigned char device_status_prev = 0xFF;
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
#define AUTO_START_DELAY_MSEC (4000)
unsigned long auto_start_last_time_stamp;

uint32_t value_tbl_idx = 0;

uint64_t volume_mil = 10000; //270000;
uint32_t unit_price = 1000;//423000;
uint32_t volume_inc_ml = 30 * 1000;//2 * 1000; 
bool is_running_button_pressed = false;
bool is_nozzel_up = false;

void loop()
{
    switch (device_status)
    {
    case waiting_for_button_press:
        if (is_nozzel_up)
        {
            if (digitalRead(c_pin_button) == HIGH)
            {
                delay(50);
                create_buffer(volume_mil, unit_price, is_nozzel_up);
                volume_mil += volume_inc_ml;
                buffer_idx = 0;
                error = 0;
                blink_delay = 175;
                digitalWrite(c_pin_red_led, HIGH);
                is_running_button_pressed = true;
            }
            else
            {
                digitalWrite(c_pin_green_led, LOW);
                is_running_button_pressed = false;
            }
        }
        else
        {
        }

        if (digitalRead(c_pin_button_Tester) == LOW)
        {

            if (is_nozzel_up)
                is_nozzel_up = false;
            else
                is_nozzel_up = true;

            Serial.println("Button Pressed...");
            delay(50);
            while (digitalRead(c_pin_button_Tester) == LOW)
                ;
            Serial.println("Button Released...");

            if (is_nozzel_up)
            {
                error = 0;
                blink_delay = 250;
                volume_mil = 0;
                digitalWrite(c_pin_red_led, HIGH);
                create_buffer(volume_mil, unit_price, is_nozzel_up);
            }
            else
            {
                digitalWrite(c_pin_red_led, LOW);
                create_buffer(volume_mil, unit_price, is_nozzel_up);
            }
        }
        buffer_idx = 0;
        device_status = sending;
        break;
    case sending:
        if (repeat_count > 0)
        {
            repeat_count--;
        }

        for (bite = 0; bite < 8; bite++)
        {
            send_byte(channel1_tx_buffer[buffer_idx + bite], channel2_tx_buffer[buffer_idx + bite]);
        }
        last_sent_buffer_idx = buffer_idx;
        buffer_idx += 8;

        device_status = waiting_for_7ms;
        ts = millis();
        break;
    case waiting_for_7ms:
        if (millis() - ts >= 7)
        {
            for (bite = 0; bite < 6; bite++)
            {
                send_byte(channel1_tx_buffer[buffer_idx + bite], channel2_tx_buffer[buffer_idx + bite]);
            }
            buffer_idx += 6;
            device_status = waiting_for_900ms;
            ts = millis();
        }
        break;
    case waiting_for_900ms:
        if (millis() - ts >= 700)
        {
            ts = millis();
            device_status = waiting_for_button_press;
        }
        break;
    default:
        break;
    }

    static bool green_led_value = HIGH;
    static unsigned long ts_green_led_blink = 0;
    if (is_running_button_pressed)
    {
        if ((millis() - ts_green_led_blink) > 75)
        {
            ts_green_led_blink = millis();
            if (green_led_value)
            {
                digitalWrite(c_pin_green_led, HIGH);
                green_led_value = LOW;
            }
            else
            {
                digitalWrite(c_pin_green_led, LOW);
                green_led_value = HIGH;
            }
        }
    }
}