flash esp
=========
windows
-------
python -m esptool --chip esp32 -b 460800  --port COM8 write-flash --flash-mode dio --flash-size 4MB --flash-freq 40m 0x1000 bootloader/bootloader.bin 0x10000 esp_data_capture.bin 0x8000 partition_table/partition-table.bin

linux
------
python -m esptool --chip esp32 -b 460800  --port /dev/ttyUSB0 write_flash 0x1000 bin_files/bootloader/bootloader.bin 0x10000 bin_files/esp_data_capture.bin 0x8000 bin_files/partition_table/partition-table.bin

rapi linux
----------
./flash_esp32.py --chip esp32 -b 460800  --port /dev/serial0 write_flash 0x1000 bin_files/bootloader/bootloader.bin 0x10000 bin_files/esp_data_capture.bin 0x8000 bin_files/partition_table/partition-table.bin

open seral terminal
===================
windows
-------
python -m serial.tools.miniterm COM8 115200

linux
------
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200

rapi linux
----------
python3 -m serial.tools.miniterm /dev/serial0 115200

SSH using Command Line
======================
- ping 100.102.234.15
- ssh username@192.168.1.32
- ssh pi@100.102.234.15
    raspberry

Transfer files using scp command
================================
- scp build/esp_data_capture.bin pi@100.102.234.15:/home/pi/Documents/bin_files
- scp build/esp_data_capture.bin pi@100.98.167.123:/home/pi/documents/bin_files

100.115.139.38