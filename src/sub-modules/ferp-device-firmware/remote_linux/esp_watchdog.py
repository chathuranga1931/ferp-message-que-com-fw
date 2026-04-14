#!/usr/bin/env python3
import serial
import time
import RPi.GPIO as GPIO
import os
import errno
import sys

# --- Config ---
# EN_PIN = 23             # ESP32 EN (reset)
# IO0_PIN = 24            # ESP32 IO0 (boot mode) - keep high
EN_PIN = 17    # ESP32 EN (reset) 17
IO0_PIN = 27   # ESP32 IO0 (boot mode) 27
SERIAL_PORT = "/dev/serial0"
BAUD_RATE = 115200
TIMEOUT_S = 30          # 30 seconds inactivity threshold
PIPE_PATH = "/tmp/esp_output"  # in-memory output (RAM only)


def setup_gpio():
    print("""Initialize GPIO pins for ESP32 control.""")
    
    GPIO.cleanup()
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(EN_PIN, GPIO.OUT)
    GPIO.setup(IO0_PIN, GPIO.OUT)

    # Normal run mode
    GPIO.output(IO0_PIN, GPIO.HIGH)
    GPIO.output(EN_PIN, GPIO.HIGH)

def reset_esp():
    print("""Toggle EN pin to reset ESP32.""")
    print(f"[*] Timeout ({TIMEOUT_S}s): restarting ESP32")
    GPIO.output(EN_PIN, GPIO.LOW)
    time.sleep(0.1)
    GPIO.output(EN_PIN, GPIO.HIGH)
    time.sleep(0.5)

def create_pipe():
    print("""Create a named pipe (FIFO) in /tmp for in-memory output.""")
    if not os.path.exists(PIPE_PATH):
        os.mkfifo(PIPE_PATH)

def open_pipe():
    # print("""Open the pipe persistently for writing.""")
    global pipe_fd
    try:
        pipe_fd = os.open(PIPE_PATH, os.O_WRONLY | os.O_NONBLOCK)
    except OSError:
        pipe_fd = None

def write_to_pipe(line: str):
    # print("""Write a line to the pipe, reopen if broken or no reader.""")
    global pipe_fd
    if pipe_fd is None:
        open_pipe()
    if pipe_fd is not None:
        try:
            os.write(pipe_fd, (line + "\n").encode())
        except OSError as e:
            if e.errno in (errno.EPIPE, errno.ENXIO):
                # No reader, close fd and reset
                try:
                    os.close(pipe_fd)
                except Exception:
                    pass
                pipe_fd = None


def main():
    print("""Main loop: read serial lines, reset ESP on timeout, forward output.""")
    setup_gpio()
    create_pipe()
    open_pipe()

    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.1)
    except serial.SerialException as e:
        print(f"[!] Failed to open serial port {SERIAL_PORT}: {e}", file=sys.stderr)
        GPIO.cleanup()
        sys.exit(1)

    print("[*] ESP Watchdog started. Listening on serial...")

    last_rx = time.time()

    try:
        while True:
            line = ser.readline()
            now = time.time()

            if line:
                line_str = line.decode(errors="ignore").strip()
                print(line_str)
                write_to_pipe(line_str)
                last_rx = now
            elif (now - last_rx) > TIMEOUT_S:
                reset_esp()
                last_rx = now

            time.sleep(0.001)

    except KeyboardInterrupt:
        print("\n[!] Stopped by user.")
    except Exception as e:
        print(f"[!] Unexpected error: {e}", file=sys.stderr)
    finally:
        ser.close()
        GPIO.cleanup()
        if os.path.exists(PIPE_PATH):
            os.remove(PIPE_PATH)
        print("[*] Clean exit.")


if __name__ == "__main__":
    main()
