#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include "DS1307RTC.h"
#include "board_inf.h"
#include "cmd_distap.h"
#include "production_test.h"

// ---- Test result tracking -----------------------------------------------
static test_result_t s_testResults[MAX_TEST_RESULTS];
static int           s_testCount = 0;

static void recordResult(const char *name,
                         test_result_status_t status,
                         const char *detail = "")
{
    if (s_testCount >= MAX_TEST_RESULTS) return;
    test_result_t &r = s_testResults[s_testCount++];
    strncpy(r.name,   name,   TEST_NAME_LEN   - 1); r.name[TEST_NAME_LEN   - 1] = '\0';
    strncpy(r.detail, detail, TEST_DETAIL_LEN - 1); r.detail[TEST_DETAIL_LEN - 1] = '\0';
    r.status = status;
}

static void printTestResultsTable()
{
    Serial.println();
    Serial.println("+------------------------------+--------+---------------------------+");
    Serial.println("| Test                         | Result | Detail                    |");
    Serial.println("+------------------------------+--------+---------------------------+");

    bool anyFail = false;
    for (int i = 0; i < s_testCount; i++)
    {
        const char *statusStr;
        switch (s_testResults[i].status)
        {
            case TEST_RESULT_PASS: statusStr = "PASS  "; break;
            case TEST_RESULT_FAIL: statusStr = "FAIL  "; anyFail = true; break;
            default:               statusStr = "SKIP  "; break;
        }
        char row[80];
        snprintf(row, sizeof(row), "| %-28s | %s | %-25s |",
                 s_testResults[i].name,
                 statusStr,
                 s_testResults[i].detail);
        Serial.println(row);
    }

    Serial.println("+------------------------------+--------+---------------------------+");
    if (anyFail)
    {
    Serial.println("|                      *** PRODUCTION FAILED ***                    |");
    }
    else
    {
        Serial.println("|                      *** PRODUCTION PASSED ***                |");
    }
    Serial.println("+------------------------------+--------+---------------------------+");
    Serial.println();
}
// -------------------------------------------------------------------------

#define get_pin(x, y) (bool)((x >> y)&0x1)

#define D1_SCLK 0
#define D1_RCLK 1
#define D1_SDATA1 2
#define D1_SDATA2 3

#define D2_SCLK 4
#define D2_RCLK 5
#define D2_SDATA1 6
#define D2_SDATA2 7

#define PIN_MAP(XX)              \
    XX(D1_SDATA1, true,  "D1_SDATA1") \
    XX(D1_SCLK,   true,  "D1_SCLK")   \
    XX(D1_RCLK,   true,  "D1_RCLK")   \
    XX(D1_SDATA2, false, "D1_SDATA2") \
    XX(D2_SDATA1, true,  "D2_SDATA1") \
    XX(D2_SCLK,   true,  "D2_SCLK")   \
    XX(D2_RCLK,   true,  "D2_RCLK")   \
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

    recordResult("WiFi Connect", TEST_RESULT_PASS);

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

    Serial.println(&timeinfo, "Internet Time OK = %A, %B %d %Y %H:%M:%S\n\n");
    char ntpDetail[TEST_DETAIL_LEN];
    snprintf(ntpDetail, sizeof(ntpDetail), "%02d/%02d/%02d %02d:%02d:%02d",
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year % 100,
             timeinfo.tm_hour, timeinfo.tm_min,  timeinfo.tm_sec);
    recordResult("NTP Time Sync", TEST_RESULT_PASS, ntpDetail);
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
        if (status)
        {
            Serial.println("RTC read OK!");
            break;
        }
        Serial.println("RTC read failed!");
        delay(2000);
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
            recordResult("RTC EEPROM R/W", TEST_RESULT_PASS);
        }
        else
        {
            Serial.println("RTC EEPROM data read match failed!");
            recordResult("RTC EEPROM R/W", TEST_RESULT_FAIL, "Read match failed");
            failedWfKey();
        }  
    }
    else
    {
        Serial.println("RTC EEPROM data set failed!");
        recordResult("RTC EEPROM R/W", TEST_RESULT_FAIL, "Write failed");
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
    Serial.println("RTC OK..!\r\n");
    recordResult("RTC Time Set", TEST_RESULT_PASS);

    // Verify the time was correctly written by reading it back and comparing
    bool rtcReadOk = RTC.read(tm_rtc);
    if (!rtcReadOk)
    {
        Serial.println("RTC read-back failed!");
        recordResult("RTC Time Read", TEST_RESULT_FAIL, "Read failed");
        failedWfKey();
        return;
    }
    // Allow up to a 2-second drift between write and read
    int diffSec = abs((int)tm_rtc.Hour * 3600 + (int)tm_rtc.Minute * 60 + (int)tm_rtc.Second
                    - (int)tm_now.Hour  * 3600 - (int)tm_now.Minute  * 60 - (int)tm_now.Second);
    char rtcDetail[TEST_DETAIL_LEN];
    snprintf(rtcDetail, sizeof(rtcDetail), "%02d/%02d/%02d %02d:%02d:%02d",
             tm_rtc.Day, tm_rtc.Month, tm_rtc.Year % 100,
             tm_rtc.Hour, tm_rtc.Minute, tm_rtc.Second);
    if (diffSec <= 2)
    {
        Serial.println("RTC read-back OK!");
        recordResult("RTC Time Read", TEST_RESULT_PASS, rtcDetail);
    }
    else
    {
        Serial.println("RTC read-back mismatch! diff=" + String(diffSec) + "s");
        recordResult("RTC Time Read", TEST_RESULT_FAIL, rtcDetail);
        failedWfKey();
    }
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
    Serial.println("SD OK..!\r\n");

    char sdDetail[TEST_DETAIL_LEN];
    snprintf(sdDetail, sizeof(sdDetail), "%.2fGB %s",
             card_size / 1000000000.0,
             card_types_str[(int)card_type].c_str());
    recordResult("SD Card", TEST_RESULT_PASS, sdDetail);
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
    recordResult("Outputs", TEST_RESULT_PASS);
}

void testLEDs()
{
    Serial.println("Checking LEDs... \r\nIs PWR LED Light?");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }
    while (1)
    {
        delay(100);
        if (Serial.available())
            break;
    }
    Serial.println("PWR LED is OK..!\r\nAre S1, S2 and S3 LEDs blink in sequence?");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }
    while (1)
    {
        digitalWrite(ESP_LED1, HIGH);
        digitalWrite(ESP_LED2, LOW);
        distap_set_led_enable(false);
        delay(300);
        digitalWrite(ESP_LED1, LOW);
        digitalWrite(ESP_LED2, HIGH);
        distap_set_led_enable(false);
        delay(300);
        digitalWrite(ESP_LED1, LOW);
        digitalWrite(ESP_LED2, LOW);
        distap_set_led_enable(true);
        delay(300);
        if (Serial.available())
            break;
    }
    Serial.println("S1 & S2 & S3 LED is OK..!\r\n");
    recordResult("LEDs", TEST_RESULT_PASS);
}

void testCSPins()
{
    esp_err_t err = ESP_OK;
    time_t time = 0;
    input_pin_t pins = {0};
    
    Serial.println("Checking Chip Select IOs... \r\nChecking CS1 In and Out");
    while (Serial.available())
    {
        Serial.read();
        delay(1);
    }

    err = distap_set_cs1_enable(false);
    if(err != ESP_OK)
    {
        Serial.println("DISTAP communication failed!");
        recordResult("CS1 Pin", TEST_RESULT_FAIL, "Comms failed");
        failedWfKey();
        return;
    }
    time = millis();
    while (1)
    {
        err = distap_get_inputs(&pins);
        if(err != ESP_OK || millis() > (time + 1000))
        {
            Serial.println("DISTAP Chip select 1 output OFF failed!");
            recordResult("CS1 Pin OFF", TEST_RESULT_FAIL, "Output OFF failed");
            failedWfKey();
            continue;
        }
        if (pins.d1_in_cs == false)
        {
            Serial.println("DISTAP Chip select 1 output OFF OK!");
            break;
        }
        delay(100);
    }
    err = distap_set_cs1_enable(true);
    if(err != ESP_OK)
    {
        Serial.println("DISTAP communication failed!");
        recordResult("CS1 Pin ON", TEST_RESULT_FAIL, "Comms failed");
        failedWfKey();
        return;
    }
    time = millis();
    while (1)
    {
        err = distap_get_inputs(&pins);
        if(err != ESP_OK || millis() > (time + 1000))
        {
            Serial.println("DISTAP Chip select 1 output ON failed!");
            recordResult("CS1 Pin ON", TEST_RESULT_FAIL, "Output ON failed");
            failedWfKey();
            continue;
        }
        if (pins.d1_in_cs == true)
        {
            Serial.println("DISTAP Chip select 1 output ON OK!");
            break;
        }
        delay(100);
    }
    recordResult("CS1 Pin", TEST_RESULT_PASS);

    Serial.println("\r\nChecking CS2 In and Out");
    err = distap_set_cs2_enable(false);
    if(err != ESP_OK)
    {
        Serial.println("DISTAP communication failed!");
        recordResult("CS2 Pin", TEST_RESULT_FAIL, "Comms failed");
        failedWfKey();
        return;
    }
    time = millis();
    while (1)
    {
        err = distap_get_inputs(&pins);
        if(err != ESP_OK || millis() > (time + 1000))
        {
            Serial.println("DISTAP Chip select 2 output OFF failed!");
            recordResult("CS2 Pin OFF", TEST_RESULT_FAIL, "Output OFF failed");
            failedWfKey();
            continue;
        }
        if (pins.d2_in_cs == false)
        {
            Serial.println("DISTAP Chip select 2 output OFF OK!");
            break;
        }
        delay(100);
    }
    err = distap_set_cs2_enable(true);
    if(err != ESP_OK)
    {
        Serial.println("DISTAP communication failed!");
        recordResult("CS2 Pin ON", TEST_RESULT_FAIL, "Comms failed");
        failedWfKey();
        return;
    }
    time = millis();
    while (1)
    {
        err = distap_get_inputs(&pins);
        if(err != ESP_OK || millis() > (time + 1000))
        {
            Serial.println("DISTAP Chip select 2 output ON failed!");
            recordResult("CS2 Pin ON", TEST_RESULT_FAIL, "Output ON failed");
            failedWfKey();
            continue;
        }
        if (pins.d2_in_cs == true)
        {
            Serial.println("DISTAP Chip select 2 output ON OK!");
            break;
        }
        delay(100);
    }
    recordResult("CS2 Pin", TEST_RESULT_PASS);
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
    recordResult("Push Button", TEST_RESULT_PASS);
    
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
    recordResult("Input 1", TEST_RESULT_PASS);

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
    recordResult("Input 2", TEST_RESULT_PASS);

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
    recordResult("Input 3", TEST_RESULT_PASS);

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
    recordResult("Input 4", TEST_RESULT_PASS);

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
    recordResult("Input 5", TEST_RESULT_PASS);

    Serial.println("Checking Power Down Input...");
    unsigned long start_ts = millis();
    if (readInput(VIN_LOW))
    {
        Serial.println("Vin higher 10.5V detect OK\r\nReduce Vin below 10.5V");
        start_ts = millis();
        while (readInput(VIN_LOW))
        {
            if(millis() - start_ts > 10 * 1000)
            {
                Serial.println("Power down input test failed!");
                recordResult("Power Down Input", TEST_RESULT_FAIL, "VIN low timeout");
                failedWfKey();
                return;
            }
        }
        Serial.println("Increase to 12.0V");
        start_ts = millis();
        while (!readInput(VIN_LOW))
        {
            if(millis() - start_ts > 10 * 1000)
            {
                Serial.println("Power down input test failed!");
                recordResult("Power Down Input", TEST_RESULT_FAIL, "VIN high timeout");
                failedWfKey();
                return;
            }
        }
    }
    else
    {
        Serial.println("Vin below 10.5V deteck OK\r\nIncrease to 12.0V");
        start_ts = millis();
        while (!readInput(VIN_LOW))
        {
            if(millis() - start_ts > 10 * 1000)
            {
                Serial.println("Power down input test failed!");
                recordResult("Power Down Input", TEST_RESULT_FAIL, "VIN high timeout");
                failedWfKey();
                return;
            }
        }
    }
    Serial.println("Power Down Input OK!\r\n");
    recordResult("Power Down Input", TEST_RESULT_PASS);
}

void testDISTAP()
{
    char fw_ver[32] = {};
    time_t time = 0;
    esp_err_t err = ESP_OK;
    input_pin_t pins = {0};

    // Read display tap firmware version
    err = distap_get_fw_version(fw_ver);
    if (err == ESP_OK)
    {
        Serial.println("DISTAP FW Version: " + String(fw_ver));
        recordResult("DISTAP FW Version", TEST_RESULT_PASS, fw_ver);
    }
    else
    {
        Serial.println("DISTAP FW version read failed!");
        recordResult("DISTAP FW Version", TEST_RESULT_FAIL, "Read failed");
        failedWfKey();
        return;
    }

    err = distap_set_display_type(DIS_NONE);
    if(err == ESP_OK)
    {
        Serial.println("DISTAP Coms OK..!\r\n" );
        recordResult("DISTAP Comms", TEST_RESULT_PASS);
    }
    else
    {
        Serial.println("DISTAP communication failed!");
        recordResult("DISTAP Comms", TEST_RESULT_FAIL, "Set display failed");
        failedWfKey();
        return;
    }

    for (size_t i = 0; i < sizeof(pin_number)/sizeof(uint32_t); i++)
    {
        char resultName[32];
        snprintf(resultName, sizeof(resultName), "DISTAP %s", pin_names[i].c_str());

        //enable inputs
        err = distap_set_inputenable(true);
        if(err != ESP_OK)
        {
            Serial.println("DISTAP output enable failed!");
            recordResult(resultName, TEST_RESULT_FAIL, "Enable failed");
            failedWfKey();
            continue;
        }

        //wait until turn on input
        Serial.println("Turn ON input " +  pin_names[i]);
        err = distap_get_inputs(&pins);
        while ( err!= ESP_OK || !get_pin(pins.u32int, pin_number[i]))
        {
            err = distap_get_inputs(&pins);
        }

        Serial.println("Turn OFF input " +  pin_names[i]);
        err = distap_get_inputs(&pins);
        while ( err!= ESP_OK || get_pin(pins.u32int, pin_number[i]))
        {
            err = distap_get_inputs(&pins);
        }

        //disable inputs and check inputs are disabled
        err = distap_set_inputenable(false);
        if(err != ESP_OK)
        {
            Serial.println("DISTAP output disable failed!");
            recordResult(resultName, TEST_RESULT_FAIL, "Disable failed");
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
            distap_get_inputs(&pins);
            if(get_pin(pins.u32int, pin_number[i]) == pin_dis_state[i])
                break;
            delay(200);
        }
        //if timeout reached, mark an error
        if(millis() - time > 10 * 1000)
        {
            Serial.println("Disabling input " +  pin_names[i] + " failed!");
            recordResult(resultName, TEST_RESULT_FAIL, "Disable state timeout");
            failedWfKey();
            continue;
        }
    end:
        //indicated output is OK
        Serial.println("Input " +  pin_names[i] + " OK!\r\n");
        recordResult(resultName, TEST_RESULT_PASS);
    }
}

void productionTest()
{
    s_testCount = 0;

    Serial.print("\r\n=======================\r\n"
                     "Stating Prodcution Test\r\n"
                     "=======================\r\n");

    // Read and record the WiFi MAC address as the very first entry
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macStr[TEST_DETAIL_LEN];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println("MAC Address: " + String(macStr));
    recordResult("MAC Address", TEST_RESULT_PASS, macStr);
    
    connectWiFi();
    testRTC();
    testDISTAP();
    testSDcard();
    testOutputs();
    testLEDs();
    testCSPins();
    testInputs();

    printTestResultsTable();
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