#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include "DS1307RTC.h"
#include "board_2404.h"
#include "cmd_esp07.h"
#include "production_test.h"

#define get_pin(x, y) (bool)((x >> y)&0x1)

#define DIS_ENB 15 //GPIO_NUM_15

#define D1_SCLK 14 //GPIO_NUM_14
#define D1_RCLK 12 //GPIO_NUM_12
#define D1_SDATA1 13 //GPIO_NUM_13
#define D1_SDATA2 16 //GPIO_NUM_16, doesn't have pull up circuit inside

#define D2_SCLK 4 //GPIO_NUM_4
#define D2_RCLK 5 //GPIO_NUM_5
#define D2_SDATA1 2 //GPIO_NUM_2
#define D2_SDATA2 0 //GPIO_NUM_0

#define PIN_MAP(XX)              \
    XX(D1_SCLK,   true,  "D1_SCLK")   \
    XX(D1_RCLK,   true,  "D1_RCLK")   \
    XX(D1_SDATA1, true,  "D1_SDATA1") \
    XX(D1_SDATA2, false, "D1_SDATA2") \
    XX(D2_SCLK,   true,  "D2_SCLK")   \
    XX(D2_RCLK,   true,  "D2_RCLK")   \
    XX(D2_SDATA1, true,  "D2_SDATA1") \
    XX(D2_SDATA2, true,  "D2_SDATA2") 

const uint32_t pin_number[] = {
#define XX(PIN, DIS_STATE, NAME) PIN,
    PIN_MAP(XX)
#undef XX
};

const bool pin_dis_state[] = {
#define XX(PIN, DIS_STATE, NAME) DIS_STATE,
    PIN_MAP(XX)
#undef XX
};

const String pin_names[] = {
#define XX(PIN, DIS_STATE, NAME) NAME,
    PIN_MAP(XX)
#undef XX
};

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;
struct tm timeinfo;

void failedWfKey()
{
    Serial.println("\r\nPress any key to continue...\r\n");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }

    while (Serial.available() == 0)
    {
        delay(1);
    }
}

void connectWiFi()
{
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("WiFi connected");
    Serial.println("IP address: " + WiFi.localIP().toString());
    Serial.println("Signal Tx:" + String(WiFi.getTxPower()) + "  RSSI:" + String(WiFi.RSSI()) + "\r\n");

    Serial.println("Getting internet time...");

    // init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    while (1)
    {
        bool status = getLocalTime(&timeinfo);
        if (status)
            break;
        Serial.println("Internet time get failed!");
        failedWfKey();
    }

    Serial.println(&timeinfo, "Internet Time = %A, %B %d %Y %H:%M:%S");
}

void testRTC()
{
    bool status;
    uint8_t hour, min, sec, mday, mon, wday;
    uint16_t year;
    tmElements_t tm_rtc, tm_now;
    while (1)
    {
        RTC.read(tm_rtc);
        status = RTC.chipPresent();
        // bool status = RTC_DS1307.begin();
        if (status)
        {
            Serial.println("RTC read OK!");
            break;
        }
        // Wire.end();
        Serial.println("RTC read failed!");
        delay(2000);
        // continue;
        // failedWfKey();
    }

    // write EEPROM data
    board_meta_data_t data = {.board_type = BOARD_TYPE};
    status = RTC.setEEEPROMdata(&data, EEPROM_ADD_BOARD, sizeof(board_meta_data_t));
    if(status)
    {
        data.board_type = 0;
        status = RTC.getEEEPROMdata(&data, EEPROM_ADD_BOARD, sizeof(board_meta_data_t));
        if(status && data.board_type == BOARD_TYPE)
        {
            Serial.println("RTC Board Set Version:" + String(data.board_type) + " OK!");
        }
        else
        {
            Serial.println("RTC EEPROM data read match failed!");
            failedWfKey();
        }  
    }
    else
    {
        Serial.println("RTC EEPROM data set failed!");
        failedWfKey();
    }

    while (1)
    {
        status = getLocalTime(&timeinfo);
        if (status)
            break;
        Serial.println("Internet time get failed!");
        failedWfKey();
    }
    tm_now.Second = timeinfo.tm_sec;
    tm_now.Minute = timeinfo.tm_min;
    tm_now.Hour = timeinfo.tm_hour;
    tm_now.Day = timeinfo.tm_mday;
    tm_now.Month = timeinfo.tm_mon;
    tm_now.Wday = timeinfo.tm_wday;
    tm_now.Year = timeinfo.tm_year;

    RTC.write(tm_now);
    RTC.read(tm_rtc);
    Serial.println("Time Stamp = " + String(tm_rtc.Day) + "/" + String(tm_rtc.Month) + "/" + String(tm_rtc.Year) + " @ " + String(tm_rtc.Hour) + ":" + String(tm_rtc.Minute) + ":" + String(tm_rtc.Second));
    // Wire.end();
    Serial.println("RTC OK..!\r\n");
}

void testSDcard()
{
    while (1)
    {
        bool status = SD.begin(SPI_CS_SD);
        if (status)
            break;
        SD.end();
        Serial.println("SD card read failed!");
        failedWfKey();
    }

    const String card_types_str[] = {"CARD_NONE", "CARD_MMC", "CARD_SD", "CARD_SDHC", "CARD_UNKNOWN"};
    uint64_t card_size = SD.cardSize();
    sdcard_type_t card_type = SD.cardType();

    Serial.println("\r\nSD Size:" + String(card_size / 1000000000.0) + "GB Card type:" + card_types_str[(int)card_type]);
    // File root = {};
    // root = SD.open("/");
    // printDirectory(root, 0);
    // SD.end();
    Serial.println("SD OK..!\r\n");
}

void printDirectory(File dir, int numTabs)
{
    while (true)
    {

        File entry = dir.openNextFile();
        if (!entry)
        {
            // no more files
            break;
        }
        for (uint8_t i = 0; i < numTabs; i++)
        {
            Serial.print('\t');
        }
        Serial.print(entry.name());
        if (entry.isDirectory())
        {
            Serial.println("/");
            printDirectory(entry, numTabs + 1);
        }
        else
        {
            // files have sizes, directories do not
            Serial.print("\t\t");
            Serial.println(entry.size(), DEC);
        }
        entry.close();
    }
}

void testUART()
{
#define BUFF_SIZE 50
    const char *test_string = "Testing_Text";

    String readbuff;
    readbuff.reserve(BUFF_SIZE);
    Serial.println("Testing COMTTL...");

    while (1)
    {
        // String readbuff;
        while (Serial2.available())
        {
            Serial2.read();
        }
        Serial.print('.');
        Serial2.print(test_string);
        delay(10);
        while (Serial2.available())
        {
            Serial.write(Serial2.read());
        }
        // int data = Serial2.available();
        // size_t data = Serial2.read((char*)readbuff.c_str(), BUFF_SIZE);
        // Serial.print("size = " + String(data) + " str: " + readbuff);
        // while (Serial2.available())
        // {
        //     readbuff = Serial2.readString();
        // }
        // Serial.println(readbuff);
        // if(readbuff.equals(String(test_string)))
        //     break;
        delay(500);
    }
    Serial.println("COMTTL OK..!\r\n");
}

void testOutputs()
{
    Serial.println("Checking Outputs... \r\nare they blinking in sequence??");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }
    while (1)
    {
        digitalWrite(OUTPUT1, HIGH);
        digitalWrite(OUTPUT2, LOW);
        digitalWrite(OUTPUT3, LOW);
        digitalWrite(OUTPUT4, LOW);
        digitalWrite(OUTPUT5, LOW);
        digitalWrite(OUTPUT6, LOW);
        // digitalWrite(OUTPUT7, LOW);
        delay(300);
        digitalWrite(OUTPUT1, LOW);
        digitalWrite(OUTPUT2, HIGH);
        digitalWrite(OUTPUT3, LOW);
        digitalWrite(OUTPUT4, LOW);
        digitalWrite(OUTPUT5, LOW);
        digitalWrite(OUTPUT6, LOW);
        // digitalWrite(OUTPUT7, LOW);
        delay(300);
        digitalWrite(OUTPUT1, LOW);
        digitalWrite(OUTPUT2, LOW);
        digitalWrite(OUTPUT3, HIGH);
        digitalWrite(OUTPUT4, LOW);
        digitalWrite(OUTPUT5, LOW);
        digitalWrite(OUTPUT6, LOW);
        // digitalWrite(OUTPUT7, LOW);
        delay(300);
        digitalWrite(OUTPUT1, LOW);
        digitalWrite(OUTPUT2, LOW);
        digitalWrite(OUTPUT3, LOW);
        digitalWrite(OUTPUT4, HIGH);
        digitalWrite(OUTPUT5, LOW);
        digitalWrite(OUTPUT6, LOW);
        // digitalWrite(OUTPUT7, LOW);
        delay(300);
        digitalWrite(OUTPUT1, LOW);
        digitalWrite(OUTPUT2, LOW);
        digitalWrite(OUTPUT3, LOW);
        digitalWrite(OUTPUT4, LOW);
        digitalWrite(OUTPUT5, HIGH);
        digitalWrite(OUTPUT6, LOW);
        // digitalWrite(OUTPUT7, LOW);
        delay(300);
        digitalWrite(OUTPUT1, LOW);
        digitalWrite(OUTPUT2, LOW);
        digitalWrite(OUTPUT3, LOW);
        digitalWrite(OUTPUT4, LOW);
        digitalWrite(OUTPUT5, LOW);
        digitalWrite(OUTPUT6, HIGH);
        // digitalWrite(OUTPUT7, LOW);
        delay(300);
        digitalWrite(OUTPUT1, LOW);
        digitalWrite(OUTPUT2, LOW);
        digitalWrite(OUTPUT3, LOW);
        digitalWrite(OUTPUT4, LOW);
        digitalWrite(OUTPUT5, LOW);
        digitalWrite(OUTPUT6, LOW);
        // digitalWrite(OUTPUT7, HIGH);
        delay(300);
        digitalWrite(OUTPUT1, LOW);
        digitalWrite(OUTPUT2, LOW);
        digitalWrite(OUTPUT3, LOW);
        digitalWrite(OUTPUT4, LOW);
        digitalWrite(OUTPUT5, LOW);
        digitalWrite(OUTPUT6, LOW);
        // digitalWrite(OUTPUT7, LOW);
        delay(300);

        if (Serial.available())
            break;
    }
    Serial.println("Outputs are OK..!\r\n");
}
bool readInput(int pin)
{
    unsigned long myTime = millis();
    if (digitalRead(pin))
    {
        while (1)
        {
            if (!digitalRead(pin))
                return false;
            if ((millis() - myTime) > 100)
                return true;
        }
    }
    return false;
}
void testInputs()
{
    Serial.println("Checking Push Button...");
    if (readInput(SWITCH))
    {
        Serial.println("Already ON Push Button, Release button");
        while (readInput(SWITCH))
        {
            /* code */
        }
    }
    else
    {
        Serial.println("Press Push Button");
        while (!readInput(SWITCH))
        {
            /* code */
        }
        Serial.println("Release Push Button");
        while (readInput(SWITCH))
        {
            /* code */
        }
    }
    Serial.println("Push Button OK!\r\n");
    
    Serial.println("Checking INPUT 1...");
    if (readInput(INPUT1))
    {
        Serial.println("Already ON INPUT 1, Turn OFF");
        while (readInput(INPUT1))
        {
            /* code */
        }
    }
    else
    {
        Serial.println("Turn ON INPUT 1");
        while (!readInput(INPUT1))
        {
            /* code */
        }
    }
    Serial.println("INPUT 1 OK!\r\n");

    Serial.println("Checking INPUT 2...");
    if (readInput(INPUT2))
    {
        Serial.println("Already ON  INPUT 2, Turn OFF");
        while (readInput(INPUT2))
        {
            /* code */
        }
    }
    else
    {
        Serial.println("Turn ON INPUT 2");
        while (!readInput(INPUT2))
        {
            /* code */
        }
    }
    Serial.println("INPUT 2 OK!\r\n");

    Serial.println("Checking INPUT 3...");
    if (readInput(INPUT3))
    {
        Serial.println("Already ON  INPUT 3, Turn OFF");
        while (readInput(INPUT3))
        {
            /* code */
        }
    }
    else
    {
        Serial.println("Turn ON INPUT 3");
        while (!readInput(INPUT3))
        {
            /* code */
        }
    }
    Serial.println("INPUT 3 OK!\r\n");

    Serial.println("Checking INPUT 4...");
    if (readInput(INPUT4))
    {
        Serial.println("Already ON  INPUT 4, Turn OFF");
        while (readInput(INPUT4))
        {
            /* code */
        }
    }
    else
    {
        Serial.println("Turn ON INPUT 4");
        while (!readInput(INPUT4))
        {
            /* code */
        }
    }
    Serial.println("INPUT 4 OK!\r\n");

    Serial.println("Checking INPUT 5...");
    if (readInput(INPUT5))
    {
        Serial.println("Already ON  INPUT 5, Turn OFF");
        while (readInput(INPUT5))
        {
            /* code */
        }
    }
    else
    {
        Serial.println("Turn ON INPUT 5");
        while (!readInput(INPUT5))
        {
            /* code */
        }
    }
    Serial.println("INPUT 5 OK!\r\n");

    Serial.println("Checking Power Down Input...");
    if (readInput(VIN_LOW))
    {
        Serial.println("Vin higher 10.5V detect OK\r\nReduce Vin below 10.5V");
        while (readInput(VIN_LOW))
        {
            /* code */
        }
        Serial.println("Increase to 12.0V");
        while (!readInput(VIN_LOW))
        {
            /* code */
        }
    }
    else
    {
        Serial.println("Vin below 10.5V deteck OK\r\nIncrease to 12.0V");
        while (!readInput(VIN_LOW))
        {
            /* code */
        }
    }
    Serial.println("Power Down Input OK!\r\n");
}

void testESP07()
{
    char fw_ver[256] = {};
    time_t time = 0;
    esp_err_t err = ESP_OK;
    uint32_t pins = 0;
    
    // err = esp07_get_fw_version(fw_ver);
    err = esp07_set_display_type(DIS_NONE);
    if(err == ESP_OK)
    {
        // Serial.println("ESP07 FW:" + String(fw_ver) + ", ESP07 Coms OK..!\r\n" );
        Serial.println("ESP07 Coms OK..!\r\n" );
    }
    else
    {
        Serial.println("ESP07 communication failed!");
        failedWfKey();
        return;
    }

    for (size_t i = 0; i < sizeof(pin_number)/sizeof(uint32_t); i++)
    {
        //enable inputs
        err = esp07_set_inputenable(true);
        if(err != ESP_OK)
        {
            Serial.println("ESP07 output enable failed!");
            failedWfKey();
            continue;
        }

        //wait until turn on input
        Serial.println("Turn ON input " +  pin_names[i]);
        err = esp07_get_inputs(&pins);
        while ( err!= ESP_OK || !get_pin(pins, pin_number[i]))
        {
            err = esp07_get_inputs(&pins);
        }

        Serial.println("Turn OFF input " +  pin_names[i]);
        err = esp07_get_inputs(&pins);
        while ( err!= ESP_OK || get_pin(pins, pin_number[i]))
        {
            err = esp07_get_inputs(&pins);
        }

        //disable inputs and check inputs are disabled o
        err = esp07_set_inputenable(false);
        if(err != ESP_OK)
        {
            Serial.println("ESP07 output disable failed!");
            failedWfKey();
            continue;
        }
        // skip disable check if pin is 16
        if(pin_number[i] == D1_SDATA2)
            goto end;
        //check disabling output with timeout
        time = millis();
        while (millis() - time <= 5 * 1000)
        {
            esp07_get_inputs(&pins);
            if(get_pin(pins, pin_number[i]) == pin_dis_state[i])
                break;
            // Serial.println(pins, HEX);
            delay(200);
        }
        //if timeout reached, mark an error
        if(millis() - time > 10 * 1000)
        {
            Serial.println("Disabling input " +  pin_names[i] + " failed!");
            failedWfKey();
            continue;
        }
    end:
        //indicated output is OK
        Serial.println("Input " +  pin_names[i] + " OK!\r\n");  
    }
    
}

void productionTest()
{
    Serial.print("\r\n=======================\r\n"
                     "Stating Prodcution Test\r\n"
                     "=======================\r\n");

    connectWiFi();
    testRTC();
    testESP07();
    testSDcard();
    testOutputs();
    testInputs();

    Serial.print("\r\n=================\r\n"
                 "Prodcution PASS!\r\n"
                 "=================\r\n");
}

void i2c_scanner()
{
    byte error, address;
    int nDevices;

    Serial.println("Scanning...");

    nDevices = 0;
    for (address = 1; address < 127; address++)
    {
        // The i2c_scanner uses the return value of
        // the Write.endTransmisstion to see if
        // a device did acknowledge to the address.
        Wire.beginTransmission(address);
        error = Wire.endTransmission();

        if (error == 0)
        {
            Serial.print("I2C device found at address 0x");
            if (address < 16)
                Serial.print("0");
            Serial.print(address, HEX);
            Serial.println("  !");

            nDevices++;
        }
        else if (error == 4)
        {
            Serial.print("Unknown error at address 0x");
            if (address < 16)
                Serial.print("0");
            Serial.println(address, HEX);
        }
    }
    if (nDevices == 0)
        Serial.println("No I2C devices found\n");
    else
        Serial.println("done\n");

}