#!/usr/bin/env python3
"""
ferp_mqtt_ota.py — CLI OTA firmware update tool for FERP devices over MQTT.

Delegates all session logic to messages/ota_session.py so that the GUI and CLI
share a single implementation.

Usage:
    python ferp_mqtt_ota.py \
        --broker 192.168.1.100 --port 1883 \
        --dev-type ferp-com --group default --device-id AA:BB:CC:DD:EE:FF \
        --target main \
        --firmware firmware_v1.2.3.bin

Targets:
    main  -- ESP32 main application firmware
    sub1  -- Sub-processor (e.g. ESP07 / display tap)
"""

import argparse
import os
import sys
import threading

from messages.ota_session import OtaSession, crc32_of_file


# ---------------------------------------------------------------------------
# Progress rendering
# ---------------------------------------------------------------------------

def _progress_bar(percent: int, width: int = 40) -> str:
    filled = int(width * percent / 100)
    bar = "\u2588" * filled + "\u2591" * (width - filled)
    return f"[{bar}] {percent:3d}%"


# ---------------------------------------------------------------------------
# Main OTA flow
# ---------------------------------------------------------------------------

def run_ota(args: argparse.Namespace) -> None:
    if not os.path.isfile(args.firmware):
        print(f"[error] Firmware file not found: {args.firmware}", file=sys.stderr)
        sys.exit(1)

    fw_size = os.path.getsize(args.firmware)
    fw_crc  = crc32_of_file(args.firmware)
    print(f"Firmware : {args.firmware}")
    print(f"Size     : {fw_size} bytes")
    print(f"CRC32    : 0x{fw_crc:08X}")
    print(f"Target   : {args.target}")
    print(f"Chunk    : {args.chunk_size} bytes")
    print(f"\nConnecting to {args.broker}:{args.port} ...")

    done_event = threading.Event()
    result     = [False]

    def on_log(msg: str) -> None:
        print(msg, flush=True)

    def on_progress(pct: int) -> None:
        print(f"\r  {_progress_bar(pct)}  ", end="", flush=True)

    def on_done(ok: bool) -> None:
        if ok:
            print("\n<- OTA complete! Device will reboot.")
        else:
            print("\n[error] OTA failed or was aborted.", file=sys.stderr)
        result[0] = ok
        done_event.set()

    session = OtaSession(
        broker        = args.broker,
        port          = args.port,
        dev_type      = args.dev_type,
        group         = args.group,
        device_id     = args.device_id,
        firmware_path = args.firmware,
        target        = args.target,
        version       = args.version,
        chunk_size    = args.chunk_size,
        ctrl_timeout  = args.ctrl_timeout,
        chunk_timeout = args.chunk_timeout,
        max_retries   = args.max_retries,
        on_log        = on_log,
        on_progress   = on_progress,
        on_done       = on_done,
    )
    session.start()
    done_event.wait()
    sys.exit(0 if result[0] else 1)


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="FERP OTA firmware update over MQTT",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--broker",        required=True)
    parser.add_argument("--port",          type=int,   default=1883)
    parser.add_argument("--dev-type",      required=True,  dest="dev_type")
    parser.add_argument("--group",         required=True)
    parser.add_argument("--device-id",     required=True,  dest="device_id")
    parser.add_argument("--target",        required=True,  choices=["main", "sub1"],
                        help="Firmware target: 'main' or 'sub1'")
    parser.add_argument("--firmware",      required=True,
                        help="Path to firmware binary (.bin)")
    parser.add_argument("--version",       default="unknown",
                        help="Version string passed to device (informational)")
    parser.add_argument("--chunk-size",    type=int,   default=4096,  dest="chunk_size",
                        help="OTA chunk size in bytes (default: 4096)")
    parser.add_argument("--ctrl-timeout",  type=float, default=15.0,  dest="ctrl_timeout",
                        help="Timeout for ctrl command responses in seconds (default: 15)")
    parser.add_argument("--chunk-timeout", type=float, default=10.0,  dest="chunk_timeout",
                        help="Timeout per chunk ack in seconds (default: 10)")
    parser.add_argument("--max-retries",   type=int,   default=3,     dest="max_retries",
                        help="Max chunk retries before aborting (default: 3)")
    return parser.parse_args()


if __name__ == "__main__":
    run_ota(parse_args())
