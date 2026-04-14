Installation in Windows Machine
===============================
- https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/windows-setup.html
- download following package
    - https://dl.espressif.com/dl/esp32_win32_msys2_environment_and_toolchain-20181001.zip
- extract it to C:\msys32
- clone the latest RTOS SDK to following location
    - C:\msys32\home\Asanga\esp
    - https://github.com/espressif/ESP8266_RTOS_SDK/tree/master
- add following line to the end fo the file "C:\msys32\home\username\.bash_profile"
    # Set Enviroment Path for this specific profile
    export IDF_PATH="$HOME/esp/ESP8266_RTOS_SDK"
    export PATH="$PATH:$HOME/esp/xtensa-lx106-elf/bin"
- open following exe file.
    - "C:\msys32\mingw32.exe"
- run following to install python requirements
    - python -m pip install --user -r $IDF_PATH/requirements.txt
- run following to install packages if want
    - python -m pip install pyelftools==0.22
    - pacman -S <package name>
- run this command to open the directory
    - cd esp/${projectname}
- run following command to see make options.
    - make menuconfig
- run following command to build the files.
    - make -j6

Commands for the project
========================
- run following command to open the menuconfig of RTOS
    - make menuconfig
    - then can edit the COM ports, baud rate, etc.
- run following command to flash the chip
    - make flash
- run following command to open the flash monitor
    - make monitor

Exporting Binaries
==================
- run following command to export binaries to a 'bin_files' folder
    - make exportbinfiles
    or
- press Ctrl+Shift+B withing VS code to export bin files.


#Flashing argumenst no OTA
--------------------------
--chip esp8266 
--port COM4 
--baud 921600 
--before default_reset 
--after hard_reset write_flash -z 
--flash_mode dio 
--flash_freq 40m 
--flash_size 4MB 
0x00000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/bootloader/bootloader.bin 
0x10000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/rtos_dis_tap_esp07.bin 
0x08000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/partitions_table.bin



#Flashing argumenst OTA
-----------------------
--chip esp8266 
--port COM4 
--baud 921600 
--before default_reset 
--after hard_reset write_flash -z 
--flash_mode dio 
--flash_freq 40m 
--flash_size 4MB 
0x00000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/bootloader/bootloader.bin 
0x0d000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/ota_data_initial.bin 
0x10000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/rtos_dis_tap_esp07.bin 
0x08000 /home/Asanga/esp/device-software/2303/main_board/rtos_sdk_esp07/build/partitions_two_ota.bin


DRC Build PC
$ export IDF_PATH=~/esp/ESP8266_RTOS_SDK
$ export PATH=$PATH:/opt/xtensa-lx106-elf/bin

Folder Path
-----------
 cd esp/device-software/2303/main_board/rtos_sdk_esp07