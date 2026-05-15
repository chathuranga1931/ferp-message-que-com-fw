import socket

LISTEN_IP = "0.0.0.0"
LISTEN_PORT = 22222

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((LISTEN_IP, LISTEN_PORT))
print(f"Listening on {LISTEN_IP}:{LISTEN_PORT} — press Ctrl+C to stop\n")

try:
    while True:
        data, addr = sock.recvfrom(4096)
        print(f"[{addr[0]}:{addr[1]}] {repr(data)}")
except KeyboardInterrupt:
    print("\nStopped.")
finally:
    sock.close()
