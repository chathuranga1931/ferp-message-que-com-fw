"""
webapi_ota.py — OTA firmware update over WebAPI (HTTP) for FERP devices.

Firmware endpoint:
    POST /api/ota/bin?name=<target>
    Content-Type: multipart/form-data
    Body: binary firmware file as a form field named "file"
    Response: { "ok": true, "bytes": <n> }

Valid target names: main, dt-bootloader, dt-partitions, dt-app
"""

import io
import os
import threading
import uuid
import argparse


class WebApiOtaError(Exception):
    pass


class _ProgressReader:
    """
    File-like wrapper around a bytes buffer.
    Calls on_progress(pct) each time the percentage advances.
    Stops feeding data if abort_flag is set (causes requests to send empty
    body remainder, which the server rejects cleanly).
    """

    def __init__(self, data: bytes, on_progress, abort_flag):
        self._buf         = io.BytesIO(data)
        self._total       = len(data)
        self._sent        = 0
        self._on_progress = on_progress
        self._abort_flag  = abort_flag
        self._last_pct    = -1

    def read(self, size: int = -1) -> bytes:
        if self._abort_flag.is_set():
            return b""
        chunk = self._buf.read(size)
        if chunk:
            self._sent += len(chunk)
            pct = min(99, int(self._sent * 100 / self._total)) if self._total else 0
            if pct != self._last_pct:
                self._last_pct = pct
                self._on_progress(pct)
        return chunk

    def __len__(self) -> int:
        return self._total


class WebApiOtaHandler:
    """
    Handles OTA firmware upload over HTTP/WebAPI.

    Sends a single multipart POST to /api/ota/bin?name=<target>.
    Progress is reported locally as bytes are streamed to the device;
    100% is reported only after the server confirms success.
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

        fw_size  = os.path.getsize(self.firmware_path)
        filename = os.path.basename(self.firmware_path)

        self._on_log(f"Firmware : {filename}")
        self._on_log(f"Size     : {fw_size:,} bytes")
        self._on_log(f"Target   : {self.target}  version={self.version}")

        # Read entire firmware into memory so we know the total size upfront
        # (needed to calculate Content-Length and percent accurately).
        try:
            with open(self.firmware_path, "rb") as fh:
                fw_data = fh.read()
        except OSError as exc:
            self._on_log(f"[error] Could not read firmware: {exc}")
            self._on_done(False)
            return

        # Build a minimal multipart/form-data body manually so we can stream
        # it through _ProgressReader without needing extra dependencies.
        boundary = uuid.uuid4().hex
        part_header = (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
            f"Content-Type: application/octet-stream\r\n"
            f"\r\n"
        ).encode()
        part_footer = f"\r\n--{boundary}--\r\n".encode()
        body        = part_header + fw_data + part_footer

        reader = _ProgressReader(body, self._on_progress, self._abort_flag)

        url    = f"{self.base_url}/api/ota/bin"
        params = {"name": self.target}

        try:
            self._on_log(f"→ POST {url}?name={self.target}  ({fw_size:,} B) ...")
            resp = self._requests.post(
                url,
                params=params,
                data=reader,
                headers={
                    "Content-Type": f"multipart/form-data; boundary={boundary}",
                    "Content-Length": str(len(body)),
                },
                timeout=300,
            )

            if self._abort_flag.is_set():
                self._on_log("Abort requested")
                self._on_done(False)
                return

            body_resp = resp.json()
            if resp.status_code != 200 or not body_resp.get("ok"):
                raise WebApiOtaError(
                    f"OTA failed (HTTP {resp.status_code}): "
                    f"{body_resp.get('error', body_resp)}"
                )

            self._on_progress(100)
            written = body_resp.get("bytes", fw_size)
            self._on_log(f"← OTA complete! {written:,} B written. Device will reboot.")
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
