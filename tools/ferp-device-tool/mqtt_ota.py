"""
mqtt_ota.py — OTA firmware update over MQTT for FERP devices.

Delegates session logic to messages/ota_session.py so GUI and CLI
share a single implementation.

Can also be used as a standalone CLI:
    python mqtt_ota.py --broker 192.168.1.1 --port 1883 \\
        --dev-type ferp-com --group default --device-id AA:BB:CC:DD:EE:FF \\
        --target main --firmware firmware_v1.2.bin
"""

import argparse
import os
import sys
import threading

from messages.ota_session import OtaSession, crc32_of_file


def _progress_bar(percent: int, width: int = 40) -> str:
    filled = int(width * percent / 100)
    bar = "█" * filled + "░" * (width - filled)
    return f"[{bar}] {percent:3d}%"


class MqttOtaHandler:
    """
    Wraps OtaSession for MQTT-based OTA.
    The GUI creates an instance and calls start(); it provides callbacks
    for log, progress, and completion.
    """

    def __init__(
        self,
        broker: str,
        port: int,
        dev_type: str,
        group: str,
        device_id: str,
        firmware_path: str,
        target: str = "main",
        version: str = "unknown",
        chunk_size: int = 4096,
        on_log=None,
        on_progress=None,
        on_done=None,
    ):
        self._session = OtaSession(
            broker=broker,
            port=port,
            dev_type=dev_type,
            group=group,
            device_id=device_id,
            firmware_path=firmware_path,
            target=target,
            version=version,
            chunk_size=chunk_size,
            on_log=on_log or (lambda m: None),
            on_progress=on_progress or (lambda p: None),
            on_done=on_done or (lambda ok: None),
        )

    def start(self) -> None:
        self._session.start()

    def abort(self) -> None:
        self._session.abort()


# ─────────────────────────────────────────────────────────────────────────────
# CLI entry point
# ─────────────────────────────────────────────────────────────────────────────

def _cli() -> None:
    ap = argparse.ArgumentParser(description="FERP OTA over MQTT")
    ap.add_argument("--broker",    required=True)
    ap.add_argument("--port",      type=int, default=1883)
    ap.add_argument("--dev-type",  default="ferp-com")
    ap.add_argument("--group",     default="default")
    ap.add_argument("--device-id", required=True)
    ap.add_argument("--target",    default="main", choices=["main", "sub1"])
    ap.add_argument("--firmware",  required=True)
    ap.add_argument("--version",   default="unknown")
    ap.add_argument("--chunk-size", type=int, default=4096)
    args = ap.parse_args()

    if not os.path.isfile(args.firmware):
        print(f"[error] File not found: {args.firmware}", file=sys.stderr)
        sys.exit(1)

    fw_size = os.path.getsize(args.firmware)
    fw_crc  = crc32_of_file(args.firmware)
    print(f"Firmware : {args.firmware}")
    print(f"Size     : {fw_size} bytes")
    print(f"CRC32    : 0x{fw_crc:08X}")

    done_event = threading.Event()
    result     = [False]

    def on_done(ok: bool):
        result[0] = ok
        done_event.set()

    handler = MqttOtaHandler(
        broker=args.broker,
        port=args.port,
        dev_type=args.dev_type,
        group=args.group,
        device_id=args.device_id,
        firmware_path=args.firmware,
        target=args.target,
        version=args.version,
        chunk_size=args.chunk_size,
        on_log=lambda m: print(m, flush=True),
        on_progress=lambda p: print(f"\r  {_progress_bar(p)}  ", end="", flush=True),
        on_done=on_done,
    )
    handler.start()
    done_event.wait()
    sys.exit(0 if result[0] else 1)


if __name__ == "__main__":
    _cli()
