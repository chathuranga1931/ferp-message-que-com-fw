import socket
import sys
import datetime
import threading
import os
import time

# Define the +5:30 offset
offset = datetime.timedelta(hours=5, minutes=30)
ist_timezone = datetime.timezone(offset)

# Configuration
LISTEN_IP = "0.0.0.0"
LISTEN_PORT = 22222
BACKUP_INTERVAL_MINUTES = 30

current_log_files = {}
last_backup_time = {}
lock = threading.Lock()

MAC_MAPPING = {
    "48E729331048" : "TESTDEV-SP01",
    "A0B765ED56C4" : "NAWINNA-P01",
    "A0B765ED56AC" : "NAWINNA-P05",
    "A0B765ED56F4" : "NAWINNA-D01",
    "A0B765ED56B8" : "NAWINNA-D03",
    "A0B765ED56BC" : "NAWINNA-SD01",
    "A0B765ED56B4" : "NAWINNA-P03",
    "A0B765ED56F8" : "NAWINNA-SD03",
    "A0B765ED56EC" : "NAWINNA-SP01",
    "48E729330FBC" : "NAWINNA-D03-2",
    "CCDBA73E9860" : "TESTDEV-PTR-1",
    "2CBCBB5A4D58" : "NAWINNA-PTR01",
    "48E729330EE0" : "MAKOLA-PTR01",
    "A0B765ED56D4" : "NUELIYA-SP01SP022",
    "A0B765ED56CC" : "NUELIYA-D04SD02",
    "A0B765ED5698" : "NUELIYA-K01K02",
    "A0B765ED56D8" : "NUELIYA-P07",
    "A0B765ED56A4" : "NUELIYA-D01D02",
    "A0B765ED56E8" : "NUELIYA-D03SD01",
    "A0B765ED56D0" : "NUELIYA-P01P02",
    "A0B765ED56C8" : "NUELIYA-P03P06",
    "A0B765ED56B8" : "NUELIYA-P04P05",
    "A0B765ED5684" : "NUELIYA-D05",
    "2CBCBB5A4E5C" : "NUELIYA-PTR00",
    "2CBCBB5A4D90" : "NUELIYA-PTR01",
    "2CBCBB5A4D64" : "NUELIYA-PTR02",
    "2CBCBB5A4D50" : "NUELIYA-PTR03",
    "2CBCBB5A4D60" : "NUELIYA-PTR04",    
    "C8F09E2E0610" : "NUELIYA-SP01SP02",
    "78421C23642C" : "NUELIYA-D01D02",
    "48E729331080" : "WANDURAMBA-PTR01",
    "78421C244988" : "WANDURAMBA-P03",
    "78421C23E2C8" : "WANDURAMBA-P01",
    "78421C24C5D0" : "WANDURAMBA-D01",
    "78421C249B6C" : "PINNA-P01",
    "78421C240730" : "PINNA-P01-1",
    "78421C23D164" : "PINNA-P03",
    "78421C235D50" : "PINNA-D01",
    "2CBCBB5A4D7C" : "BANDA-PTR1",
    "78421C23EFFC" : "BANDA-D01",
    "78421C242B58" : "BANDA-P03", 
    "78421C241F98" : "POLGAS-P01",
    "A0B765ED56C0" : "POLGAS-D01",
    "48E729331044" : "POLGAS-P04",
    "0C8B95F1C800" : "POLGAS-P02",
    "78421C249B6C" : "PINNA-SD02",
    "78421C8B9F18" : "PINNA-PTR1",
    "78421C235FC0" : "WELI-D01",
    "A0B765ED56A4" : "POLGAS-SP02",
    "112233445566" : "TEST-P01"
    
}

def get_mapped_value(mac):
    return MAC_MAPPING.get(mac, "UNKNOWN")

def process_message(message):
    try:
        mac, msg = message.split(" : ", 1)
        mapped_value = get_mapped_value(mac)
        return mapped_value, msg
    except ValueError:
        return "Invalid format", message

def get_log_directory(file_prefix):
    daily_folder = get_daily_folder()
    directory_name = os.path.join(daily_folder, file_prefix.split("-")[0])
    if not os.path.exists(directory_name):
        os.makedirs(directory_name)
    return directory_name

def get_log_filename(file_prefix):
    now = datetime.datetime.now(ist_timezone)
    directory = get_log_directory(file_prefix)
    return os.path.join(directory, f"{file_prefix}-{now.strftime('%Y%m%d-%H%M')}.txt")

def get_daily_folder():
    today = datetime.datetime.now(ist_timezone).strftime('%Y-%m-%d')
    if not os.path.exists(today):
        os.makedirs(today)
    return today

def log_message(file_prefix, message):
    with lock:
        now = datetime.datetime.now(ist_timezone)
        if file_prefix not in last_backup_time or (now - last_backup_time[file_prefix]).total_seconds() >= BACKUP_INTERVAL_MINUTES * 60:
            current_log_files[file_prefix] = get_log_filename(file_prefix)
            last_backup_time[file_prefix] = now

        log_filename = current_log_files[file_prefix]
        with open(log_filename, "a") as log_file:
            log_file.write(message + "\n")

def main():
    # Create a UDP socket
    udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # Bind the socket
    try:
        udp_socket.bind((LISTEN_IP, LISTEN_PORT))
        print(f"Listening for UDP packets on {LISTEN_IP}:{LISTEN_PORT}...")
    except Exception as e:
        print(f"Failed to bind socket: {e}")
        sys.exit(1)

    try:
        while True:
            try:
                data, addr = udp_socket.recvfrom(1024)
                message = data.decode('utf-8', errors='replace').strip()
                sender_ip = addr[0]

                mapped_name, message_part = process_message(message)

                current_time = datetime.datetime.now(ist_timezone).strftime("%Y-%m-%d %H:%M:%S")
                updated_message = f"[{current_time}] {message_part}"

                print(f"{mapped_name} : {updated_message}")

                log_message(mapped_name, updated_message)
            except KeyboardInterrupt:
                print("\nKeyboardInterrupt detected. Exiting...")
                break
            except Exception as e:
                print(f"[ERROR] Failed to process packet from {addr}: {e}")
    finally:
        udp_socket.close()
        print("Socket closed. Goodbye!")

if __name__ == "__main__":
    while True:
        try:
            main()
            break  # Only exit if main() exits normally (e.g., on KeyboardInterrupt)
        except KeyboardInterrupt:
            print("Exiting on user request.")
            sys.exit(0)
        except Exception as e:
            print(f"An error occurred: {e}")
            print("Restarting the script in 2 seconds...")
            time.sleep(2)
            # Restart this script with the same arguments
            os.execv(sys.executable, [sys.executable] + sys.argv)