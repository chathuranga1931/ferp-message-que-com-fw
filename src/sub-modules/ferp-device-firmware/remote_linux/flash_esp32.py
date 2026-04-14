#!/usr/bin/env python3
import RPi.GPIO as GPIO
import time
import subprocess
import sys

# --- Config ---
# EN_PIN = 23    # ESP32 EN (reset)
# IO0_PIN = 24   # ESP32 IO0 (boot mode)
EN_PIN = 17    # ESP32 EN (reset)
IO0_PIN = 27   # ESP32 IO0 (boot mode)

BOOT_DELAY = 0.5
RESET_DELAY = 0.1

def gpio_setup():
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(EN_PIN, GPIO.OUT)
    GPIO.setup(IO0_PIN, GPIO.OUT)
    # Force normal run state first (important)
    GPIO.output(IO0_PIN, GPIO.HIGH)
    GPIO.output(EN_PIN, GPIO.HIGH)
    time.sleep(RESET_DELAY)

def enter_bootloader():
    print("[*] Entering bootloader mode...")
    GPIO.output(IO0_PIN, GPIO.LOW)
    time.sleep(RESET_DELAY)
    GPIO.output(EN_PIN, GPIO.LOW)
    time.sleep(RESET_DELAY)
    GPIO.output(EN_PIN, GPIO.HIGH)
    time.sleep(BOOT_DELAY)

def reset_normal():
    print("[*] Resetting to normal run mode...")
    GPIO.output(IO0_PIN, GPIO.HIGH)
    time.sleep(RESET_DELAY)
    GPIO.output(EN_PIN, GPIO.LOW)
    time.sleep(RESET_DELAY)
    GPIO.output(EN_PIN, GPIO.HIGH)
    time.sleep(BOOT_DELAY)

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} [esptool args...]")
        sys.exit(1)

    # Split args into global args and subcommand args
    user_args = sys.argv[1:]

    try:
        cmd_index = user_args.index("write_flash")
        global_args = user_args[:cmd_index]
        subcommand_args = user_args[cmd_index:]
    except ValueError:
        global_args = user_args
        subcommand_args = []

    # Base esptool command
    esptool_args = [sys.executable, "-m", "esptool"]

    # --- AUTO ADD --no-stub FOR APT ESPTOOL ---
    try:
        import esptool
        if esptool.__file__.startswith("/usr/lib/python3"):
            print("[!] Detected system esptool (apt). Adding --no-stub")
            global_args.insert(0, "--no-stub")
    except Exception:
        pass

    esptool_args += global_args + subcommand_args

    reset_after = any(arg == "write_flash" for arg in user_args)

    gpio_setup()
    try:
        enter_bootloader()
        subprocess.check_call(esptool_args)
        if reset_after:
            reset_normal()
        else:
            print("[*] Staying in bootloader mode (no reset).")
    finally:
        GPIO.output(IO0_PIN, GPIO.HIGH)
        GPIO.output(EN_PIN, GPIO.HIGH)
        GPIO.cleanup()

if __name__ == "__main__":
    main()
