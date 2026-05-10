"""
webapi_ota.py — OTA firmware update over WebAPI (HTTP) for FERP devices.

The WebAPI OTA protocol uses chunked HTTP POST to /api/ota/upload.
This is a placeholder / stub that can be fleshed out once the
firmware-side WebAPI OTA endpoint is implemented.

The GUI instantiates WebApiOtaHandler in the same way as MqttOtaHandler.
"""

import os
import sys
import threading
import time
import argparse


class WebApiOtaError(Exception):
    pass


class WebApiOtaHandler:
    """
    Handles OTA firmware upload over HTTP/WebAPI.

    Protocol (draft):
        POST /api/ota/start
             { "target": "main", "size": <bytes>, "version": "1.2.3" }
             -> { "ok": true }

        POST /api/ota/chunk
             Content-Type: application/octet-stream
             Headers: X-OTA-Offset: <int>
             Body: <binary chunk>
             -> { "ok": true, "offset_next": <int> }

        POST /api/ota/complete
             { "crc32": "0x..." }
             -> { "ok": true }
    """

    def __init__(
        self,
        ip: str,
        port: int,
        firmware_path: str,
        target: str = "main",
        version: str = "unknown",
        chunk_size: int = 4096,
        on_log=None,
        on_progress=None,
        on_done=None,
    ):
        try:
            import requests
            self._requests = requests
        except ImportError:
            raise WebApiOtaError("requests not installed — run: pip install requests")

        self.base_url     = f"http://{ip.strip()}:{port}/api/ota"
        self.firmware_path = firmware_path
        self.target       = target
        self.version      = version
        self.chunk_size   = chunk_size
        self._on_log      = on_log      or (lambda m: None)
        self._on_progress = on_progress or (lambda p: None)
        self._on_done     = on_done     or (lambda ok: None)
        self._abort_flag  = threading.Event()

    def start(self) -> None:
        t = threading.Thread(target=self._run, daemon=True)
        t.start()

    def abort(self) -> None:
        self._abort_flag.set()

    def _post(self, path: str, headers: dict = None, **kwargs) -> dict:
        resp = self._requests.post(
            f"{self.base_url}/{path}", timeout=10, headers=headers, **kwargs)
        resp.raise_for_status()
        return resp.json()

    def _run(self) -> None:
        if not os.path.isfile(self.firmware_path):
            self._on_log(f"[error] Firmware file not found: {self.firmware_path}")
            self._on_done(False)
            return

        import zlib
        fw_size = os.path.getsize(self.firmware_path)

        # CRC32
        crc = 0
        with open(self.firmware_path, "rb") as fh:
            while chunk := fh.read(65536):
                crc = zlib.crc32(chunk, crc)
        crc &= 0xFFFF_FFFF
        crc_str = f"0x{crc:08X}"

        self._on_log(f"Firmware : {os.path.basename(self.firmware_path)}")
        self._on_log(f"Size     : {fw_size:,} bytes  CRC32={crc_str}")
        self._on_log(f"Target   : {self.target}  version={self.version}")

        try:
            # 1. Start
            self._on_log(f"→ POST {self.base_url}/start ...")
            resp = self._post("start", json={
                "target": self.target, "size": fw_size,
                "version": self.version, "crc32": crc_str,
            })
            if not resp.get("ok"):
                raise WebApiOtaError(f"OTA start rejected: {resp}")
            self._on_log("← OTA start accepted")

            # 2. Chunks
            offset = 0
            with open(self.firmware_path, "rb") as fh:
                while True:
                    if self._abort_flag.is_set():
                        self._on_log("Abort requested")
                        self._on_done(False)
                        return
                    data = fh.read(self.chunk_size)
                    if not data:
                        break
                    resp = self._post("chunk",
                                      headers={"X-OTA-Offset": str(offset)},
                                      data=data)
                    if not resp.get("ok"):
                        raise WebApiOtaError(f"Chunk rejected at offset {offset}: {resp}")
                    offset = resp.get("offset_next", offset + len(data))
                    self._on_progress(int(offset * 100 / fw_size))

            # 3. Complete
            self._on_log("→ POST complete ...")
            resp = self._post("complete", json={"crc32": crc_str})
            if not resp.get("ok"):
                raise WebApiOtaError(f"OTA complete rejected: {resp}")

            self._on_progress(100)
            self._on_log("← OTA complete! Device will reboot.")
            self._on_done(True)

        except WebApiOtaError as exc:
            self._on_log(f"[error] {exc}")
            self._on_done(False)
        except Exception as exc:
            self._on_log(f"[error] Unexpected error: {exc}")
            self._on_done(False)


# ─────────────────────────────────────────────────────────────────────────────
# CLI entry point
# ─────────────────────────────────────────────────────────────────────────────

def _cli() -> None:
    ap = argparse.ArgumentParser(description="FERP OTA over WebAPI")
    ap.add_argument("--ip",       required=True)
    ap.add_argument("--port",     type=int, default=8080)
    ap.add_argument("--target",   default="main", choices=["main", "sub1"])
    ap.add_argument("--firmware", required=True)
    ap.add_argument("--version",  default="unknown")
    ap.add_argument("--chunk-size", type=int, default=4096)
    args = ap.parse_args()

    done_event = threading.Event()
    result     = [False]

    def on_done(ok: bool):
        result[0] = ok
        done_event.set()

    handler = WebApiOtaHandler(
        ip=args.ip,
        port=args.port,
        firmware_path=args.firmware,
        target=args.target,
        version=args.version,
        chunk_size=args.chunk_size,
        on_log=lambda m: print(m, flush=True),
        on_progress=lambda p: print(f"\r  {p:3d}%  ", end="", flush=True),
        on_done=on_done,
    )
    handler.start()
    done_event.wait()
    sys.exit(0 if result[0] else 1)


if __name__ == "__main__":
    _cli()
