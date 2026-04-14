"C:\Users\Asanga\.platformio\packages\tool-esptoolpy\esptool.py" 
    --chip esp32 
    --port "COM8" 
    --baud 921600 
    --before default_reset 
    --after hard_reset 
    write_flash -z 
    --flash_mode dio 
    --flash_freq 40m 
    --flash_size 4MB 
        0x1000 .pio\build\esp32dev\bootloader.bin 
        0x8000 .pio\build\esp32dev\partitions.bin 
        0xe000 C:\Users\Asanga\.platformio\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin 
        0x10000 .pio\build\esp32dev\firmware.bin
