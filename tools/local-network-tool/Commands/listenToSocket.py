import sys
import asyncio
import websockets

RECONNECT_DELAY = 1  # seconds

async def listen_ws(ip):
    url = f"ws://{ip}/webserialws"
    while True:
        print(f"Connecting to {url} ...")
        try:
            async with websockets.connect(url) as ws:
                print("✅ WebSocket connected")
                while True:
                    try:
                        msg = await ws.recv()
                        print(msg)
                    except websockets.ConnectionClosed:
                        print("❌ WebSocket disconnected")
                        break
        except Exception as e:
            print(f"⚠️ WebSocket error: {str(e)}")
        print(f"Reconnecting in {RECONNECT_DELAY} seconds...")
        await asyncio.sleep(RECONNECT_DELAY)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python listenWebSocket.py <ip-address>")
        sys.exit(1)

    ip_address = sys.argv[1]
    asyncio.run(listen_ws(ip_address))
