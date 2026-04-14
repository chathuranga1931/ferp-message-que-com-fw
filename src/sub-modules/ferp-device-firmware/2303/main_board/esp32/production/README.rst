 --chip esp8266 
 --port COM39 
 --baud 921600 
 --before default_reset 
 --after hard_reset write_flash -z 
 --flash_mode dio 
 --flash_freq 40m 
 --flash_size 4MB 
    0x00000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/bootloader/bootloader.bin 
    0x10000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/rtos_dis_tap_esp07.bin 
    0x08000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/partitions_table.bin