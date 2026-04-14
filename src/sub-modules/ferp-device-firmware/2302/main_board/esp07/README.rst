"C:\Users\Asanga\.platformio\packages\tool-esptoolpy@1.30000.201119\esptool.py" 
    --before no_reset 
    --after soft_reset 
    --chip esp8266 
    --port "COM8" 
    --baud 921600 
    write_flash 
        0x0 .pio\build\esp8285\firmware.bin