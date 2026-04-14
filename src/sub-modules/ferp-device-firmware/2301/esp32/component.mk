COMPONENT_SOC := esp32
HWCONFIG := spiffs
SPIFF_FILES = web/dist
ARDUINO_LIBRARIES := ArduinoJson6
COMPONENT_DEPENDS += MDNS
ENABLE_SSL ?= Bearssl

COM_PORT=COM4
COM_SPEED_ESPTOOL=512000
APP_NAME=Iot_Module_V1.0.0

APP_CFLAGS := -DAPP_NAME=$(APP_NAME)

ifndef MAKE_DOCS

cleanbuild:
	$(Q) make clean
	$(Q) make -j6

web-upload:
	$(Q) make clean
	$(Q) npm run dist
	$(Q) make -j6
	$(Q) make flash

writeflash: 
	$(Q) $(PYTHON) $(SMING_HOME)/Components/esptool/esptool/esptool.py -p $(COM_PORT) -b $(COM_SPEED_ESPTOOL) write_flash 0x00000 flash_4M.bin

readflash: 
	$(Q) $(PYTHON) $(SMING_HOME)/Components/esptool/esptool/esptool.py -p $(COM_PORT) -b $(COM_SPEED_ESPTOOL) read_flash 0x00000 0x400000 flash_4M.bin

exportbuild:
	$(Q) mkdir -p bin_files
	$(Q) cp $(FW_BASE)/rboot.bin bin_files/
	$(Q) cp $(FW_BASE)/rom0.bin bin_files/
	$(Q) cp $(FW_BASE)/partitions.bin bin_files/
	$(Q) cp $(FW_BASE)/spiff_rom.bin bin_files/
	$(Q) cp $(FLASH_INIT_DATA) bin_files/
	$(Q) echo App=$(APP_NAME) > bin_files/$(APP_NAME).txt

writebinfiles:
	$(Q) C:\Python39\python C:\tools\Sming\Sming\Components\esptool\esptool\esptool.py -p $(COM_PORT) -b $(COM_SPEED_ESPTOOL) --chip esp8266 --before default_reset --after hard_reset write_flash -z -fs 4MB -ff 40m -fm dio 0x00000000 bin_files/rboot.bin 0x000fa000 bin_files/partitions.bin 0x00002000 bin_files/rom0.bin 0x00200000 bin_files/spiff_rom.bin 0x003fa000 bin_files/partitions.bin 0x003fc000 bin_files/esp_init_data_default.bin

endif
