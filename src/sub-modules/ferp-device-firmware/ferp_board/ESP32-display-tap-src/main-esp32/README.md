Git topic
=========
Main Board, ESP32 Production:


# Build Project with exporting binary bin_files
```
pio run -e esp32-board-2602 -t build_and_export
```

# Flashing the ESP32 (MAC OS)
```
python3 ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32 --port /dev/tty.usbserial-A5069RR4 --baud 512000 --after hard_reset write_flash 0x0 bin_files/esp32-board-2602/merged_esp32-board-2602.bin
```

# Flashing the ESP32 (Windows)

Download esptool.exe
https://github.com/espressif/esptool/releases

Paste it outside the Release folder

Execute the below command
** Update the COM Port as needed    
```
esptool --chip esp32 --port COM4 --baud 512000 --after hard_reset write_flash 0x0 merged_esp32-board-2602.bin
```
