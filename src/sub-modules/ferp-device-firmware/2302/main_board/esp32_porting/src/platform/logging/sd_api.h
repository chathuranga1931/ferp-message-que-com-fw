#ifndef _SD_API__H_
#define _SD_API__H_

#include <Arduino.h>
#include <SD.h>

#include "error.h"

ret_t sd_init(device_configs_t * device_configs);
ret_t listDir(fs::FS &fs, const char * dirname, uint8_t levels, bool first);
void createDir(fs::FS &fs, const char * path);
void removeDir(fs::FS &fs, const char * path);
void readFile(fs::FS &fs, const char * path);
void writeFile(fs::FS &fs, const char * path, const char * message);
void appendFile(fs::FS &fs, const char * path, const char * message);
void renameFile(fs::FS &fs, const char * path1, const char * path2);
void deleteFile(fs::FS &fs, const char * path);
void testFileIO(fs::FS &fs, const char * path);

#endif //#define _SD_API__H_