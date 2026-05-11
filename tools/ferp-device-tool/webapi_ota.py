"""
webapi_ota.py — OTA firmware update over WebAPI (HTTP) for FERP devices.

Firmware endpoint:
    POST /api/ota/bin?name=<target>
    Content-Type: multipart/form-data
    Body: binary firmware file as a form field named "file"
    Response: { "ok": true, "bytes": <n> }

Valid target names: main, dt-bootloader, dt-partitions, dt-app
"""

import os
import threading
import argparse


class WebApiOtaError(Exception):
    pass


class WebApiOtaHandler:
    """
    Handles OTA firmware upload over HTTP/WebAPI.

    Sends a single multipart POST to /api/ota/bin?name=<target>.
    The firmware handles the OTA handshake and write internally.
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

        self.base_url      = f"http://{ip.strip()}:{port}"
        self.firmware_path = firmware_path
        self.target        = target
        self.version       = version
        self.chunk_size    = chunk_size
        self._on_log       = on_log      or (lambda m: None)
        self._on_progress  = on_progress or (lambda p: None)
        self._on_done      = on_done     or (lambda ok: None)
        self._abort_flag   = threading.Event()

    def start(self) -> None:
        t = threading.Thread(target=self._run, daemon=True)
        t.start()

    def abort(self) -> None:
        self._abort_flag.set()

    def _run(self) -> None:
        if not os.path.isfile(self.firmware_path):
            self._on_log(f"[error] Firmware file not found: {self.firmware_path}")
            self._on_done(False)
            return

        fw_size = os.path.getsize(self.firmware_path)
        filename = os.path.basename(self.firmware_path)

        self._on_log(f"Firmware : {filename}")
        self._on_log(f"Size     : {fw_size:,} bytes")
        self._on_log(f"Target   : {self.target}  version={self.version}")

        url = f"{self.base_url}/api/ota/bin"
        params = {"name": self.target}

        try:
            self._on_log(f"→ POST {url}?name={self.target} ...")
            with open(self.firmware_path, "rb") as fh:
                resp = self._requests.post(
                    url,
                    params=params,
                    files={"file": (filename, fh, "application/octet-stream")},
                    timeout=300,
                )

            if self._abort_flag.is_set():
                self._on_log("Abort requested")
                self._on_done(False)
                return

            body = resp.json()
            if resp.status_code != 200 or not body.get("ok"):
                raise WebApiOtaError(
                    f"OTA failed (HTTP {resp.status_code}): {body.get('error', body)}"
                )

            self._on_progress(100)
            self._on_log(f"← OTA complete! {body.get('bytes', fw_size):,} B written. Device will reboot.")
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
    import sys
    ap = argparse.ArgumentParser(description="FERP OTA over WebAPI")
    ap.add_argument("--ip",       required=True)
    ap.add_argument("--port",     type=int, default=8080)
    ap.add_argument("--target",   default="main",
                    choices=["main", "dt-bootloader", "dt-partitions", "dt-app"])
    ap.add_argument("--firmware", required=True)
    ap.add_argument("--version",  default="unknown")
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
        on_log=lambda m: print(m, flush=True),
        on_progress=lambda p: print(f"\r  {p:3d}%  ", end="", flush=True),
        on_done=on_done,
    )
    handler.start()
    done_event.wait()
    sys.exit(0 if result[0] else 1)


if __name__ == "__main__":
    _cli()
