// #define IN_OUT_MAP_SIZE 10

// hardware SPI operate pins
#define DIS1_IN_CS 27 // chip select input
#define DIS2_IN_CS 12
#define DIS1_IN_RCLK 26
#define DIS1_IN_SCLK 25
#define DIS1_IN_DATA1 33
#define DIS1_IN_DATA2 32
#define DIS2_IN_RCLK 35
#define DIS2_IN_SCLK 34
#define DIS2_IN_DATA1 39
#define DIS2_IN_DATA2 36

#define DIS1_OUT_CS 14 // chip select output, controlled by software, routed to CS Pin 27
#define DIS2_OUT_CS 13
#define DIS1_OUT_RCLK 23
#define DIS1_OUT_SCLK 22
#define DIS1_OUT_DATA1 21
#define DIS1_OUT_DATA2 19
#define DIS2_OUT_RCLK 18
#define DIS2_OUT_SCLK 17
#define DIS2_OUT_DATA1 16
#define DIS2_OUT_DATA2 04

// #define PIN_IN_OUT_MAP(XX)
//   XX("DIS1_CS", DIS1_IN_CS, DIS1_OUT_CS)
//   XX("DIS2_CS", DIS2_IN_CS, DIS2_OUT_CS)

const uint8_t inputs[] = {
    DIS1_IN_CS,
    DIS2_IN_CS,
    DIS1_IN_RCLK,
    DIS1_IN_SCLK,
    DIS1_IN_DATA1,
    // DIS1_IN_DATA2,
    DIS2_IN_RCLK,
    DIS2_IN_SCLK,
    DIS2_IN_DATA1,
    // DIS2_IN_DATA2,
};

const uint8_t outputs[] = {
    DIS1_OUT_CS,
    DIS2_OUT_CS,
    DIS1_OUT_RCLK,
    DIS1_OUT_SCLK,
    DIS1_OUT_DATA1,
    // DIS1_OUT_DATA2,
    DIS2_OUT_RCLK,
    DIS2_OUT_SCLK,
    DIS2_OUT_DATA1,
    // DIS2_OUT_DATA2,
};

const String pin_name[] = {
    "DIS1_CS   ",
    "DIS2_CS   ",
    "DIS1_RCLK ",
    "DIS1_SCLK ",
    "DIS1_DATA1",
    // "DIS1_DATA2",
    "DIS2_RCLK ",
    "DIS2_SCLK ",
    "DIS2_DATA1",
    // "DIS2_DATA2",
};

const uint16_t bit_map[] = {
    0b0000000000000001,
    0b0000000000000010,
    0b0000000000000100,
    0b0000000000001000,
    0b0000000000010000,
    0b0000000000100000,
    0b0000000001000000,
    0b0000000010000000,
    0b0000000100000000,
    0b0000001000000000,
};

#define IN_OUT_MAP_SIZE map_size

const size_t map_size = sizeof(inputs)/sizeof(*inputs);

void printBinary(uint16_t val)
{
    for (int i = IN_OUT_MAP_SIZE; i >= 0; i--)
    {
        Serial.print((val >> i) & 1);
    }
}

uint16_t getPinInputs()
{
    uint16_t pins = 0;
    for (size_t i = 0; i < IN_OUT_MAP_SIZE; i++)
    {
        pins |= (uint16_t)((bool)digitalRead(inputs[i]) << i);
    }
    return pins;
}

void setPinOutputs(const uint16_t map)
{
    for (size_t i = 0; i < IN_OUT_MAP_SIZE; i++)
    {
        const bool level = (bool)(map & bit_map[i]);
        digitalWrite(outputs[i], level);
    }
}

void printPins(const uint16_t set_out_map)
{
    for (size_t i = 0; i < IN_OUT_MAP_SIZE; i++)
    {
        Serial.print("\t" + pin_name[i] + " Out:" + ((set_out_map & bit_map[i]) ? "ON " : "OFF") + " In:" + (digitalRead(inputs[i]) ? "ON " : "OFF") + "\r\n");
    }
}

// the setup routine runs once when you press reset:
void setup()
{
    // initialize serial communication at 9600 bits per second:
    Serial.begin(115200);

    // inputs
    for (size_t i = 0; i < IN_OUT_MAP_SIZE; i++)
    {
        pinMode(inputs[i], INPUT);
    }

    // output
    for (size_t i = 0; i < IN_OUT_MAP_SIZE; i++)
    {
        pinMode(outputs[i], OUTPUT);
    }

    // inputs_pre.uint8 = 0;
    Serial.println("Starting production test for adaptor board ESP32.\r\n");

    for (size_t i = 0; i < IN_OUT_MAP_SIZE; i++)
    {
        uint16_t input_pins;
        uint16_t set_out_map = bit_map[i];

        Serial.print("Testing " + pin_name[i] + " OFF:");
        set_out_map = 0;
        setPinOutputs(set_out_map);
        delay(10);
        input_pins = getPinInputs();
        if ((uint16_t)(set_out_map & input_pins) != set_out_map)
            goto failed;
        Serial.print("OK\r\n");

        Serial.print("Testing " + pin_name[i] + " ON:");
        set_out_map = bit_map[i];
        setPinOutputs(set_out_map);
        delay(10);
        input_pins = getPinInputs();
        if ((uint16_t)(set_out_map & input_pins) != set_out_map)
            goto failed;
        Serial.print("OK\r\n");

        Serial.print("Testing " + pin_name[i] + " ON with rest:");
        setPinOutputs(set_out_map);
        delay(10);
        input_pins = getPinInputs();
        if (input_pins != set_out_map)
            goto failed;
        Serial.print("OK\r\n\r\n");

        continue;
    failed:
        Serial.print("FAILED\r\n");
        Serial.print("  Inputs:0b");
        printBinary(input_pins);
        Serial.print("  Outputs:0b");
        printBinary(set_out_map);
        Serial.println();

        printPins(set_out_map);
        while (1)
        {
            delay(1);
        }
    }
}

// the loop routine runs over and over again forever:
void loop()
{
    delay(1); // delay in between reads for stability
}
