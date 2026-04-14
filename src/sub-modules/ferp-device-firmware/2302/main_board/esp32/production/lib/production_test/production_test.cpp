#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
// #include <DS1307.h>
#include "DS1307RTC.h"
#include "board_2302.h"
#include "production_test.h"


const char* ntpServer = "pool.ntp.org";
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
    uint8_t hour, min, sec, mday, mon, wday;
    uint16_t year;
    tmElements_t tm_rtc, tm_now;

    while (1)
    {
        RTC.read(tm_rtc);
        bool status = RTC.chipPresent();
        // bool status = RTC_DS1307.begin();
        if (status){
            Serial.println("RTC read OK!");
            break;
        }
        // Wire.end();
        Serial.println("RTC read failed!");
        delay(2000);
        continue;
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
    const char * test_string = "Testing_Text";
    
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
    Serial.println("Checking LEDs... \r\nare they on/off??");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }
    while (1)
    {
        digitalWrite(LED1, HIGH);
        digitalWrite(LED2, LOW);
        digitalWrite(LED3, LOW);
        delay(300);
        digitalWrite(LED1, LOW);
        digitalWrite(LED2, HIGH);
        digitalWrite(LED3, LOW);
        delay(300);
        digitalWrite(LED1, LOW);
        digitalWrite(LED2, LOW);
        digitalWrite(LED3, HIGH);
        delay(300);
        digitalWrite(LED1, LOW);
        digitalWrite(LED2, LOW);
        digitalWrite(LED3, LOW);
        delay(300);

        if(Serial.available())
            break;
    }
    Serial.println("LEDs OK..!\r\n");

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

    Serial.println("Checking OUTPUT 2... \r\nis it blinking??");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }
    while (1)
    {
        digitalWrite(OUTPUT2, HIGH);
        delay(100);
        digitalWrite(OUTPUT2, LOW);
        delay(100);
        if(Serial.available())
            break;
    }
    Serial.println("OUTPUT2 OK..!\r\n");

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

    Serial.println("Checking Push Button...");
    if(readInput(SWITCH)){
        Serial.println("Already ON Push Button, Release buttonF");
        while (readInput(SWITCH))
        {
            /* code */
        }
    }else{
        Serial.println("Press Push Button");
        while (!readInput(SWITCH))
        {
            /* code */
        }
    }
    Serial.println("Push Button OK!\r\n");

    Serial.println("Checking Voltage Input above 10.5V ...");
    if(readInput(VIN_LOW)){
        Serial.println("Already Voltage above 10.5V. Reduce it...");
        while (readInput(VIN_LOW))
        {
            /* code */
        }
    }else{
        Serial.println("Increase to 12V");
        while (!readInput(VIN_LOW))
        {
            /* code */
        }
    }
    Serial.println("Volateg low level detect OK!\r\n");



}



void productionTest()
{
    // connectWiFi();
    testRTC();
    testSDcard();
    // testUART();
    // testOutputs();
    // testInputs();

    Serial.print("\r\n=================\r\n"
                 "Prodcution PASS!\r\n"
                 "=================\r\n");
}