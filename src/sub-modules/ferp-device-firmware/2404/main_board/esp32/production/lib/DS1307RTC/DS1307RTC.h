/*
 * DS1307RTC.h - library for DS1307 RTC
 * This library is intended to be uses with Arduino Time library functions
 */

#ifndef DS1307RTC_h
#define DS1307RTC_h

#include <TimeLib.h>
#include <Wire.h>

// library interface description
class DS1307RTC
{
    // user-accessible "public" interface
public:
    DS1307RTC();

    void begin(TwoWire *twire);
    time_t get();
    bool set(time_t t);
    bool read(tmElements_t &tm);
    bool write(tmElements_t &tm);
    bool chipPresent() { return exists; }
    unsigned char isRunning();
    void setCalibration(char calValue);
    char getCalibration();
    bool getEEEPROMdata(void *data, uint8_t add_start, size_t len);
    bool setEEEPROMdata(void *data, uint8_t add_start, size_t len);

private:
    TwoWire *wire = {};
    bool exists = false;
    uint8_t dec2bcd(uint8_t num);
    uint8_t bcd2dec(uint8_t num);
};

#ifdef RTC
#undef RTC // workaround for Arduino Due, which defines "RTC"...
#endif

extern DS1307RTC RTC;

#endif
