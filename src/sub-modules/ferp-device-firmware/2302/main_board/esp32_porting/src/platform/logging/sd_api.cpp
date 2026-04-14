#include <Arduino.h>

#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <SPIFFS.h>

#include "device_config.h"
#include "logger.h"
#include "error.h"

#include "logger.h"
#include "sd_api.h"
#include "board.h"

static device_configs_t * _device_configs;

// listDir(SD, "/", 0);
// removeDir(SD, "/mydir");
// listDir(SD, "/", 2);
// writeFile(SD, "/Logs/test.log", "This is the first log message\n");
// appendFile(SD, "/Logs/test.log", "This is the second log message");
// readFile(SD, "/hello.txt");
// deleteFile(SD, "/foo.txt");
// renameFile(SD, "/hello.txt", "/foo.txt");
// readFile(SD, "/foo.txt");
// testFileIO(SD, "/test.txt");
// logger.log("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
// logger.log("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));


// ret_t listDir(fs::FS &fs, const char * dirname, uint8_t levels){

// 	// logger.log("Listing directory: %s\n", dirname);
// 	ret_t ret = ret_Success;

// 	String file_save_path = "/Logs/filelistSD.csv";
// 	deleteFile(SD, file_save_path.c_str());
// 	// File file_save_details = fs.open(file_path, );

// 	File root = fs.open(dirname);
// 	if(!root){
// 		// logger.log("Failed to open directory");
// 		return ret_Err_Hdware_FileSystem;
// 	}
// 	if(!root.isDirectory()){
// 		// logger.log("Not a directory");
// 		return ret_Err_Hdware_FileSystem;
// 	}

// 	File file = root.openNextFile();
// 	uint16_t file_count = 0;
// 	while(file){
// 		if(file.isDirectory()){
// 			// Serial.print("  DIR : ");
// 			// logger.log(file.name());
// 			if(levels){
// 				listDir(fs, file.path(), levels -1);
// 			}
// 		} 
// 		else {
// 			file_count++;
			
// 			// Serial.print("  FILE: ");
// 			// Serial.print(file.name());
// 			// Serial.print("  SIZE: ");
// 			// logger.log(file.size());
// 			if(file_count < 1000){
// 				String file_info = String(file.path()) + "," + String(file.size()) + "\n";
// 				appendFile(SD, file_save_path.c_str(), file_info.c_str());
// 			}
// 		}
// 		file = root.openNextFile();
// 	}

// 	return ret;
// }

String file_save_path = "/Logs/filelist.csv";


ret_t listDir(fs::FS &fs, const char * dirname, uint8_t levels, bool first){

	// logger.log("Listing directory: %s\n", dirname);
	ret_t ret = ret_Success;

	// File file_save_details = fs.open(file_path, );
	if(first){
		deleteFile(fs, file_save_path.c_str());
	}

	File root = fs.open(dirname);
	if(!root){
		// logger.log("Failed to open directory");
		return ret_Err_Hdware_FileSystem;
	}
	if(!root.isDirectory()){
		// logger.log("Not a directory");
		return ret_Err_Hdware_FileSystem;
	}

	File file = root.openNextFile();
	uint16_t file_count = 0;
	while(file){
		if(file.isDirectory()){
			// Serial.print("  DIR : ");
			// logger.log(file.name());
			if(levels){
				listDir(fs, file.path(), levels -1, false);
			}
		}
		else {
			file_count++;

			// Serial.print("  FILE: ");
			// Serial.print(file.name());
			// Serial.print("  SIZE: ");
			// logger.log(file.size());
			if(file_save_path != file.path()){
				if(file_count < 250){
					String file_info = String(file.path()) + "," + String(file.size()) + "\n";
					appendFile(fs, file_save_path.c_str(), file_info.c_str());
					// logger.log(file.path());
				}
				else if(file_count >= 250){
					return ret;
				}
				// logger.log(file.path());
				// deleteFile(fs, file.path());
			}
		}
		file = root.openNextFile();
	}

	return ret;
}

void createDir(fs::FS &fs, const char * path){
	// logger.log("Create file: " + String(path) );
	
	if(fs.mkdir(path)){
		// logger.log("Dir created");
	} else {
		logger.log("mkdir failed");
	}
}

void removeDir(fs::FS &fs, const char * path){
	// logger.log("Remove Dir: " + String(path) );
	if(fs.rmdir(path)){
		logger.log("Dir removed, Success");
	} else {
		logger.log("rmdir failed");
	}
}

void readFile(fs::FS &fs, const char * path){
	// logger.log("Read file: " + String(path) );

	File file = fs.open(path);
	if(!file){
		logger.log("Failed to open file for reading");
		return;
	}

	// Serial.print("Read from file: ");
	while(file.available()){
		// Serial.write(file.read());
	}
	file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message){
	logger.log("Write file: " + String(path) );
// 
	File file = fs.open(path, FILE_WRITE);
	if(!file){
		logger.log("Failed to open file for writing");
		return;
	}
	if(file.print(message)){
		// logger.log("File written");
	} else {
		logger.log("Write failed");
	}
	file.close();
}

void appendFile(fs::FS &fs, const char * path, const char * message){
	// logger.log("Appending to file: %s\n", path);

	File file = fs.open(path, FILE_APPEND);
	if(!file){
		logger.log("Failed to open file for appending");
		return;
	}
	if(file.print(message)){
		// logger.log("Message appended");
	} else {
		logger.log("Append failed");
	}
	file.close();
}

void renameFile(fs::FS &fs, const char * path1, const char * path2){
	// logger.log("Renaming file " + String(path1) + " to "  + String(path2));
	if (fs.rename(path1, path2)) {
		// logger.log("File renamed");
	} else {
		logger.log("Rename failed");
	}
}

void deleteFile(fs::FS &fs, const char * path){
	// logger.log("Deleting file: " + String(path) );

	if(fs.exists(path)){
		if(fs.remove(path)){
			// logger.log("File deleted");
		} else {
			logger.log("Delete failed");
		}
	}
}

void testFileIO(fs::FS &fs, const char * path){
	File file = fs.open(path);
	static uint8_t buf[512];
	size_t len = 0;
	uint32_t start = millis();
	uint32_t end = start;
	if(file){
		len = file.size();
		size_t flen = len;
		start = millis();
		while(len){
		size_t toRead = len;
		if(toRead > 512){
			toRead = 512;
		}
		file.read(buf, toRead);
		len -= toRead;
		}
		end = millis() - start;
		// logger.log("%u bytes read for %u ms\n", flen, end);
		file.close();
	} else {
		logger.log("Failed to open file for reading");
	}


	file = fs.open(path, FILE_WRITE);
	if(!file){
		logger.log("Failed to open file for writing");
		return;
	}

	size_t i;
	start = millis();
	for(i=0; i<2048; i++){
		file.write(buf, 512);
	}
	end = millis() - start;
	// logger.log("%u bytes written for %u ms\n", 2048 * 512, end);
	file.close();
}

ret_t sd_init(device_configs_t * device_configs){

  	ret_t ret = ret_Success;
	do{
        if(device_configs != nullptr){
            _device_configs = device_configs;
        }
        else{
            logger.log("Fundamental error, check source...");
			ret = ret_Err_Hdware;
			break;
        }

		_device_configs->status.sd_card_status = sd_card_mounted;
		if(!SD.begin(SPI_CS_SD)){
			logger.log("Card Mount Failed");
			_device_configs->status.sd_card_status = sd_card_pending;
			ret = ret_Err_Hdware;
			break;
		}

        _device_configs->sdcard_info.type = SD.cardType();
        _device_configs->sdcard_info.size = SD.cardSize();

		uint8_t cardType = SD.cardType();
		if(cardType == CARD_NONE){
            _device_configs->status.sd_card_status = sd_card_pending;
			logger.log("No SD card attached");
			ret = ret_Err_Hdware;
			break;
		}

		Serial.print("SD Card Type: ");

		if(cardType == CARD_MMC){
			logger.log("MMC");
		} else if(cardType == CARD_SD){
			logger.log("SDSC");
		} else if(cardType == CARD_SDHC){
      		_device_configs->status.sd_card_status = sd_card_mounted;
			logger.log("SDHC");
		} else {
			logger.log("UNKNOWN");
		}

		uint64_t cardSize = SD.cardSize() / (1024 * 1024);
		logger.log("SD Card Size: " + String(cardSize) + "MB\n");

		// removeDir(SD, "/");
		// removeDir(SD, "/Logs");

		ret_t ret = ret_Success;
        // listDir(SD, "/", 2, true);

		/* update file list every 30 seconds. This is not needed in final execusion.
		 * but better to have for debugging */
		listDir(SD, "/", 2, true);
		listDir(SPIFFS, "/", 2, true);

	}while(false);
	
    return ret;
}