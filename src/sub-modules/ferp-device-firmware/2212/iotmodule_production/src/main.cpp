/*
  The circuit:
   SD card attached to SPI bus as follows:
 ** MOSI - GPIO 23
 ** MISO - GPIO 19
 ** CLK - GPIO 18
 ** CS - GPIO 5 (for MKRZero SD: SDCARD_SS_PIN)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <ErriezDS1307.h>
#include <Wire.h>
#include "time.h"

#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_CLK 18
#define SPI_CS 5
#define I2C_SCL 22
#define I2C_SDA 21
#define RXD2 16
#define TXD2 17
#define OUTPUT1 26
#define BUZZER 12
#define INPUT1 32
#define INPUT2 34

#define WIFI_SSID "Optus_1BA38F"
#define WIFI_PASS "thawysandyZRwSV"

void printDirectory(File dir, int numTabs);
void testSDcard();
void failedWfKey();
void testRTC();
void printLocalTime();
void connectWiFi();
void testUART();
void testOutputs();
void testInputs();
bool readInput(int pin);

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;
struct tm timeinfo;
ErriezDS1307 RTC;

void setup()
{
    pinMode(INPUT1, INPUT);
    pinMode(INPUT2, INPUT);
    pinMode(OUTPUT1, OUTPUT);
    pinMode(BUZZER, OUTPUT);

    // Open serial communications and wait for port to open:
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
    Serial.print("\r\n\r\n============================ \r\n"
                 "Starting prodcution test...!\r\n"
                 "=============================\r\n");
}

void loop()
{
    connectWiFi();
    testRTC();
    testSDcard();
    testOutputs();
    testInputs();
    testUART();

    Serial.print("\r\n=================\r\n"
                 "Prodcution PASS!\r\n"
                 "=================\r\n");

    while (1)
        ;
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
    uint8_t hour, min, sec, mday, mon, wday;
    uint16_t year;

    while (1)
    {
        Wire.begin(I2C_SDA, I2C_SCL, 100000);
        bool status = RTC.begin();
        if (status)
            break;
        Wire.end();
        Serial.println("RTC read failed!");
        failedWfKey();
    }
    while (1)
    {
        bool status = getLocalTime(&timeinfo);
        if (status)
            break;
        Serial.println("Internet time get failed!");
        failedWfKey();
    }

    RTC.setDateTime((uint8_t)timeinfo.tm_hour, (uint8_t)timeinfo.tm_min, (uint8_t)timeinfo.tm_sec, (uint8_t)timeinfo.tm_mday, (uint8_t)timeinfo.tm_mon, (uint16_t)timeinfo.tm_year, (uint8_t)timeinfo.tm_wday);

    RTC.getDateTime(&hour, &min, &sec, &mday, &mon, &year, &wday);
    Serial.println("Time Stamp = " + String(mday) + "/" + String(mon) + "/" + String(year) + " @ " + String(hour) + ":" + String(min) + ":" + String(sec));
    Wire.end();
    Serial.println("RTC OK..!\r\n");
}

void testSDcard()
{
    while (1)
    {
        bool status = SD.begin(SPI_CS);
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
    SD.end();
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
#define TEST_STRING "Testing_Text"
    
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
        Serial2.print(TEST_STRING);
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
        // if(readbuff.equals(String(TEST_STRING)))
        //     break;
        delay(500);
    }
    Serial.println("COMTTL OK..!\r\n");
}

void testOutputs()
{
    Serial.println("Checking OUTPUT 1... \r\nis it blinking??");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }
    while (1)
    {
        digitalWrite(OUTPUT1, HIGH);
        delay(100);
        digitalWrite(OUTPUT1, LOW);
        delay(100);
        if(Serial.available())
            break;
    }
    Serial.println("OUTPUT1 OK..!\r\n");

    Serial.println("Checking BUZZER... \r\nis it beeping??");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }
    while (1)
    {
        digitalWrite(BUZZER, HIGH);
        delay(100);
        digitalWrite(BUZZER, LOW);
        delay(100);
        if(Serial.available())
            break;
    }
    Serial.println("BUZZER OK..!\r\n\r\nOUTPUTS OK..!\r\n");
}
void testInputs()
{
    Serial.println("Checking INPUT 1...");
    if(readInput(INPUT1)){
        Serial.println("Already ON INPUT 1, Turn OFF");
        while (readInput(INPUT1))
        {
            /* code */
        }
    }else{
        Serial.println("Turn ON INPUT 1");
        while (!readInput(INPUT1))
        {
            /* code */
        }
    }
    Serial.println("INPUT 1 OK!\r\n");

    Serial.println("Checking INPUT 2...");
    if(readInput(INPUT2)){
        Serial.println("Already ON  INPUT 2, Turn OFF");
        while (readInput(INPUT2))
        {
            /* code */
        }
    }else{
        Serial.println("Turn ON INPUT 2");
        while (!readInput(INPUT2))
        {
            /* code */
        }
    }
    Serial.println("INPUT 2 OK!\r\n");

}

bool readInput(int pin)
{
    unsigned long myTime = millis();
    if(digitalRead(pin)){
        while (1)
        {
            if(!digitalRead(pin))
                return false;
            if((millis() - myTime)>100)
                return true;
        }
    }
    return false;
    // else{
    //     while (1)
    //     {
    //         if(digitalRead(pin))
    //             return true;
    //         if((millis() - myTime)>10)
    //             return false;
    //     }
    // }
}


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