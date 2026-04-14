flash esp
=========
windows
-------
python -m esptool --chip esp32 -b 460800  --port COM6 write-flash --flash-mode dio --flash-size 4MB --flash-freq 40m 0x1000 bootloader/bootloader.bin 0x10000 esp_data_capture.bin 0x8000 partition_table/partition-table.bin

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
python -m serial.tools.miniterm COM6 115200

linux
------
python3 -m serial.tools.miniterm /dev/ttyUSB0 115200

rapi linux
----------
python3 -m serial.tools.miniterm /dev/serial0 115200

Setup Tailscale in ra-pi
========================
- use following to setup wifi network and disable console on serial0
    - sudo raspi-config
- use followig to retup morning restart
    - sudo crontab -e
    - then add folliwnf to end of the opened file
        0 2 * * * /sbin/shutdown -r now
    - Save (Ctrl+S) and exit. 
- flash no desktop image, headless image.
- once done, unplug and plug the SD card back to computer.
    - add "enable_uart=1" to config.txt located in "boofs" drive
- install serial reading tool to monitor esp32
    - sudo apt update
    - sudo apt upgrade
    - sudo apt install -y python3-pip python3-dev libffi-dev libssl-dev build-essential
    - sudo apt install python3-pip -y
    - pip3 install pyserial
    - python3 -m serial.tools.miniterm /dev/serial0 115200
- install esptool to flash esp32
    - python3 -m pip install --upgrade pip setuptools wheel
    - pip3 install esptool
    OR try followings
    - sudo apt update
    - sudo apt install python3-serial
    - sudo apt update
    - python3 -m pip install --upgrade pip setuptools wheel --break-system-packages
    - sudo apt install esptool
- create tailscale account.
- install taiscale using 
    https://tailscale.com/kb/1197/install-rpi-bullseye


SSH using Command Line
----------------------
- ping 100.102.234.15
- ssh username@192.168.1.32
- ssh pi@100.102.234.15
    raspberry

Tranfer files to between remote machine
======================================= 
Syntax for copying a local file to a remote server
--------------------------------------------------
- scp /path/to/local/file username@remote_host:/path/to/remote/directory
    scp partition_table/partition-table.bin asanga@192.168.1.33:/home/asanga/bin_files/partition_table
    scp bootloader/bootloader.bin asanga@192.168.1.33:/home/asanga/bin_files/bootloader
    scp esp_data_capture.bin asanga@192.168.1.33:/home/asanga/bin_files

Syntax for copying a file from a remote server to a local machine
-----------------------------------------------------------------
- scp username@remote_host:/path/to/remote/file /path/to/local/directory

Syntax for synchronizing files using rsync over SSH
---------------------------------------------------
- rsync -avz -e ssh /path/to/local/source username@remote_host:/path/to/remote/destination

Use WinSCP to tranfer files
---------------------------
- open SFTP connection using username and password
- select file/folder and send files.

ESP32 Watchdog Auto start with boot
===================================
- give permision
    - chmod +x esp_watchdog.py
- create service file
    - sudo nano /etc/systemd/system/esp_watchdog.service
    - save following (without dash lines in begining and end)
---------------------------------------------------------------
[Unit]
Description=ESP32 Serial Watchdog
After=network.target

[Service]
ExecStart=/usr/bin/python3 /home/pi/documents/esp_watchdog.py
Restart=always
User=pi
WorkingDirectory=/home/pi
# We use pipe and stdout directly to console, no SD log
StandardOutput=null
StandardError=null

[Install]
WantedBy=multi-user.target

---------------------------------------------------------------
    - sudo systemctl daemon-reload
    - sudo systemctl enable esp_watchdog.service
- start service immediately
    - sudo systemctl start esp_watchdog.service
- check state
    - sudo systemctl status esp_watchdog.service
- view serial output
    - cat /tmp/esp_output
- stop service
    - sudo systemctl stop esp_watchdog.service
- diasble auto start at boot
    - sudo systemctl disable esp_watchdog.service
- restart service
    - sudo systemctl restart esp_watchdog.service

