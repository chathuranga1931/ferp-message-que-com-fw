esptool.py -p COM9 -b 460800 --before default_reset --after hard_reset --chip esp32 write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB 
0x1000 bootloader/bootloader.bin 
0x10000 distap-esp32.bin 
0x8000 partition_table/partition-table.bin 