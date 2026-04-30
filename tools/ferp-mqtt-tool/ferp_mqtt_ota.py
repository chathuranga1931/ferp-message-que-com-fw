#!/usr/bin/env python3
"""
ferp_mqtt_ota.py

OTA firmware update tool for FERP devices over MQTT.

Protocol:
    .../ota/ctrl   — JSON control commands  (ota_start, ota_abort, ota_status)
    .../ota/data   — Binary chunks          [OFFSET 4B big-endian][DATA]
    .../ota/resp   — JSON responses from device

Usage:
    python ferp_mqtt_ota.py \\
        --broker 192.168.1.100 --port 1883 \\
        --dev-type ferp-fuel --group site_a --device-id AA:BB:CC:DD:EE:FF \\
        --target main \\
        --firmware firmware_v1.2.3.bin \\
        --chunk-size 4096

Targets:
    main   — ESP32 main application firmware
    sub1   — Sub-processor (e.g. ESP07 / display tap)
"""

import argparse
import json
import os
import struct
import sys
import time
import threading
import random
import zlib

import paho.mqtt.client as mqtt

# ---------------------------------------------------------------------------
# Topic helpers (mirrors ferp_mqtt_tool.py)
# ---------------------------------------------------------------------------

def _base(dev_type: str, group: str, device_id: str) -> str:
    return f"ferp/{dev_type}/{group}/{device_id}"

def _ota_ctrl(base: str)  -> str: return f"{base}/ota/ctrl"
def _ota_data(base: str)  -> str: return f"{base}/ota/data"
def _ota_resp(base: str)  -> str: return f"{base}/ota/resp"

# ---------------------------------------------------------------------------
# Shared state
# ---------------------------------------------------------------------------

class _OtaState:
    def __init__(self):
        self.connected          = threading.Event()
        self.ctrl_response      = threading.Event()
        self.chunk_response     = threading.Event()
        self.last_resp: dict | None = None
        self.offset_next: int   = 0


# ---------------------------------------------------------------------------
# CRC32 (matches ESP-IDF esp_crc32_le behaviour — standard zlib CRC32)
# ---------------------------------------------------------------------------

def crc32_of_file(path: str) -> int:
    crc = 0
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFF_FFFF


# ---------------------------------------------------------------------------
# MQTT callbacks
# ---------------------------------------------------------------------------

def _on_connect(client, userdata: _OtaState, flags, rc):
    if rc == 0:
        userdata.connected.set()
    else:
        print(f"[error] Connection refused (rc={rc})", file=sys.stderr)


def _on_message(client, userdata: _OtaState, message):
    try:
        payload = json.loads(message.payload.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        print(f"[warn] Bad JSON on {message.topic}: {exc}", file=sys.stderr)
        return

    userdata.last_resp = payload
    cmd = payload.get("cmd", "")

    if cmd in ("ota_start", "ota_abort", "ota_status", "ota_complete"):
        userdata.ctrl_response.set()
    elif cmd == "ota_chunk":
        userdata.offset_next = payload.get("offset_next", 0)
        userdata.chunk_response.set()


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _send_ctrl(client: mqtt.Client, state: _OtaState, base: str,
               payload: dict, timeout_s: float = 15.0) -> dict | None:
    """Publish a JSON control command and wait for the device response."""
    state.ctrl_response.clear()
    state.last_resp = None
    client.publish(_ota_ctrl(base), json.dumps(payload), qos=1)
    if not state.ctrl_response.wait(timeout=timeout_s):
        return None
    return state.last_resp


def _send_chunk(client: mqtt.Client, state: _OtaState, base: str,
                offset: int, data: bytes, timeout_s: float = 15.0) -> dict | None:
    """Publish a binary OTA chunk and wait for the device ack."""
    state.chunk_response.clear()
    state.last_resp = None

    # Frame: [OFFSET 4B big-endian][DATA]
    frame = struct.pack(">I", offset) + data
    client.publish(_ota_data(base), frame, qos=1)

    if not state.chunk_response.wait(timeout=timeout_s):
        return None
    return state.last_resp


def _progress_bar(percent: int, width: int = 40) -> str:
    filled = int(width * percent / 100)
    bar = "█" * filled + "░" * (width - filled)
    return f"[{bar}] {percent:3d}%"


# ---------------------------------------------------------------------------
# Main OTA flow
# ---------------------------------------------------------------------------

def run_ota(args: argparse.Namespace):
    firmware_path = args.firmware
    if not os.path.isfile(firmware_path):
        print(f"[error] Firmware file not found: {firmware_path}", file=sys.stderr)
        sys.exit(1)

    firmware_size = os.path.getsize(firmware_path)
    firmware_crc  = crc32_of_file(firmware_path)

    print(f"Firmware : {firmware_path}")
    print(f"Size     : {firmware_size} bytes")
    print(f"CRC32    : 0x{firmware_crc:08X}")
    print(f"Target   : {args.target}")
    print(f"Chunk    : {args.chunk_size} bytes")

    base   = _base(args.dev_type, args.group, args.device_id)
    state  = _OtaState()
    cid    = f"ferp-ota-{random.randint(1000, 9999)}"
    client = mqtt.Client(client_id=cid)
    client.user_data_set(state)
    client.on_connect = _on_connect
    client.on_message = _on_message

    print(f"\nConnecting to {args.broker}:{args.port} ...")
    client.connect(args.broker, args.port, keepalive=60)
    client.loop_start()

    if not state.connected.wait(timeout=10.0):
        print("[error] Could not connect to broker within 10s", file=sys.stderr)
        sys.exit(1)

    client.subscribe(_ota_resp(base), qos=1)
    print(f"Connected. OTA resp topic: {_ota_resp(base)}\n")

    # -----------------------------------------------------------------------
    # 1. ota_start
    # -----------------------------------------------------------------------
    print("→ Sending ota_start ...")
    resp = _send_ctrl(client, state, base, {
        "seq":     1,
        "cmd":     "ota_start",
        "data": {
            "target":  args.target,
            "size":    firmware_size,
            "version": args.version,
            "crc32":   f"0x{firmware_crc:08X}",
        }
    }, timeout_s=args.ctrl_timeout)

    if resp is None:
        print("[error] No response to ota_start", file=sys.stderr)
        client.loop_stop()
        sys.exit(1)

    if resp.get("status") != "ok":
        code = resp.get("code", "")
        print(f"[error] ota_start rejected: {code}", file=sys.stderr)
        client.loop_stop()
        sys.exit(1)

    print("← ota_start accepted\n")

    # -----------------------------------------------------------------------
    # 2. Stream chunks
    # -----------------------------------------------------------------------
    total_chunks = (firmware_size + args.chunk_size - 1) // args.chunk_size
    sent_bytes   = 0
    retries_left = args.max_retries

    with open(firmware_path, "rb") as f:
        for chunk_idx in range(total_chunks):
            offset = chunk_idx * args.chunk_size
            chunk  = f.read(args.chunk_size)

            while retries_left >= 0:
                resp = _send_chunk(client, state, base, offset, chunk,
                                   timeout_s=args.chunk_timeout)

                if resp is None:
                    retries_left -= 1
                    if retries_left < 0:
                        print(f"\n[error] No response to chunk at offset {offset}", file=sys.stderr)
                        _abort(client, state, base)
                        client.loop_stop()
                        sys.exit(1)
                    print(f"\n[warn] Chunk timeout at offset {offset}, retrying ({retries_left} left)...")
                    continue

                if resp.get("status") != "ok":
                    expected = resp.get("offset_expecting", offset)
                    print(f"\n[warn] Device expects offset {expected}, got {offset} — resyncing")
                    # Seek file to the expected offset and continue
                    f.seek(expected)
                    offset = expected
                    chunk  = f.read(args.chunk_size)
                    continue

                # Chunk accepted
                break

            sent_bytes += len(chunk)
            percent = int(sent_bytes * 100 / firmware_size)
            print(f"\r  {_progress_bar(percent)}  {sent_bytes}/{firmware_size} B", end="", flush=True)

    print()  # newline after progress bar

    # -----------------------------------------------------------------------
    # 3. ota_complete
    # -----------------------------------------------------------------------
    print("\n→ Sending ota_complete ...")
    resp = _send_ctrl(client, state, base, {
        "seq":  3,
        "cmd":  "ota_complete",
        "data": {"crc32": f"0x{firmware_crc:08X}"},
    }, timeout_s=args.ctrl_timeout)

    if resp is None:
        print("[error] No response to ota_complete", file=sys.stderr)
        client.loop_stop()
        sys.exit(1)

    if resp.get("status") != "ok":
        code = resp.get("code", "")
        print(f"[error] ota_complete failed: {code}", file=sys.stderr)
        client.loop_stop()
        sys.exit(1)

    print("← OTA complete! Device will reboot.")
    client.loop_stop()
    client.disconnect()


def _abort(client: mqtt.Client, state: _OtaState, base: str):
    print("\n→ Sending ota_abort ...")
    client.publish(_ota_ctrl(base), json.dumps({"seq": 99, "cmd": "ota_abort"}), qos=1)


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="FERP OTA firmware update over MQTT",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--broker",        required=True)
    parser.add_argument("--port",          type=int, default=1883)
    parser.add_argument("--dev-type",      required=True)
    parser.add_argument("--group",         required=True)
    parser.add_argument("--device-id",     required=True)
    parser.add_argument("--target",        required=True,
                        choices=["main", "sub1"],
                        help="Firmware target: 'main' or 'sub1'")
    parser.add_argument("--firmware",      required=True,
                        help="Path to firmware binary (.bin)")
    parser.add_argument("--version",       default="unknown",
                        help="Version string to pass to device (informational)")
    parser.add_argument("--chunk-size",    type=int, default=4096,
                        help="OTA chunk size in bytes (default: 4096)")
    parser.add_argument("--ctrl-timeout",  type=float, default=15.0,
                        help="Timeout for ctrl command responses in seconds (default: 15)")
    parser.add_argument("--chunk-timeout", type=float, default=10.0,
                        help="Timeout per chunk ack in seconds (default: 10)")
    parser.add_argument("--max-retries",   type=int, default=3,
                        help="Max chunk retries before aborting (default: 3)")
    return parser.parse_args()


if __name__ == "__main__":
    run_ota(parse_args())
