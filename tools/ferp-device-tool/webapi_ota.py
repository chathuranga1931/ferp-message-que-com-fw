"""
webapi_ota.py — OTA firmware update over WebAPI (HTTP) for FERP devices.

Chunked protocol — 3-step session, mirrors MQTT OTA:

  1. POST /api/ota/start?name=<target>
     Body:     { "size": <total_bytes>, "crc32": <uint32> }
     Response: { "ok": true, "chunk_size": 4096 }

  2. POST /api/ota/chunk?seq=<N>        (repeated for each chunk)
     Body:     raw binary  (Content-Type: application/octet-stream)
     Response: { "ok": true, "seq": N, "written": <cumulative_bytes> }
     On seq mismatch: HTTP 409  { "ok": false, "error": "seq mismatch",
                                  "expected": <N> }

  3. POST /api/ota/complete
     Body:     { "crc32": <uint32> }
     Response: { "ok": true, "bytes": <total> }

Network errors on individual chunks are retried up to _MAX_RETRIES times
before the whole OTA is aborted.

Valid target names: main, dt-bootloader, dt-partitions, dt-app
"""

import os
import threading
import zlib
import argparse

_MAX_RETRIES = 3


class WebApiOtaError(Exception):
    pass


class WebApiOtaHandler:
    """
    Handles OTA firmware upload over HTTP/WebAPI using the 3-step chunked
    protocol.  Progress 0-99% reflects per-chunk upload; 100% is set only
    after the device confirms the /api/ota/complete step.
    """

    def __init__(
        self,
        ip: str,
        port: int,
        firmware_path: str,
        target: str = "main",
        version: str = "unknown",
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
        import requests as _req

        if not os.path.isfile(self.firmware_path):
            self._on_log(f"[error] Firmware file not found: {self.firmware_path}")
            self._on_done(False)
            return

        try:
            with open(self.firmware_path, "rb") as fh:
                fw_data = fh.read()
        except OSError as exc:
            self._on_log(f"[error] Could not read firmware: {exc}")
            self._on_done(False)
            return

        fw_size  = len(fw_data)
        crc32    = zlib.crc32(fw_data) & 0xFFFFFFFF
        filename = os.path.basename(self.firmware_path)

        self._on_log(f"Firmware : {filename}")
        self._on_log(f"Size     : {fw_size:,} bytes")
        self._on_log(f"CRC32    : 0x{crc32:08X}")
        self._on_log(f"Target   : {self.target}  version={self.version}")

        sess = _req.Session()
        try:
            self._upload(sess, fw_data, fw_size, crc32)
        except WebApiOtaError as exc:
            self._on_log(f"[error] {exc}")
            self._on_done(False)
        except Exception as exc:
            self._on_log(f"[error] Unexpected: {exc}")
            self._on_done(False)
        finally:
            sess.close()

    def _upload(self, sess, fw_data: bytes, fw_size: int, crc32: int) -> None:
        import requests as _req

        # ── Step 1: start ─────────────────────────────────────────────────────
        self._on_log(f"→ POST /api/ota/start?name={self.target}")
        try:
            r = sess.post(
                f"{self.base_url}/api/ota/start",
                params={"name": self.target},
                json={"size": fw_size, "crc32": crc32},
                timeout=10,
            )
            r.raise_for_status()
        except Exception as exc:
            raise WebApiOtaError(f"start request failed: {exc}") from exc

        resp = r.json()
        if not resp.get("ok"):
            raise WebApiOtaError(f"start rejected: {resp.get('error', resp)}")

        chunk_size   = int(resp.get("chunk_size", 4096))
        total_chunks = (fw_size + chunk_size - 1) // chunk_size
        self._on_log(f"← Session open  chunk_size={chunk_size}  chunks={total_chunks}")

        # ── Step 2: chunks ────────────────────────────────────────────────────
        # Log every ~5% to keep the log readable (progress bar updates every chunk)
        log_every = max(1, total_chunks // 20)
        w         = len(str(total_chunks))   # width for zero-padded counter
        seq       = 0
        offset    = 0

        while offset < fw_size:
            if self._abort_flag.is_set():
                self._on_log("Aborted by user")
                self._on_done(False)
                return

            chunk     = fw_data[offset : offset + chunk_size]
            last_exc  = None

            for attempt in range(_MAX_RETRIES):
                try:
                    r = sess.post(
                        f"{self.base_url}/api/ota/chunk",
                        params={"seq": seq},
                        data=chunk,
                        headers={"Content-Type": "application/octet-stream"},
                        timeout=30,
                    )
                    r.raise_for_status()
                    cr = r.json()
                    if not cr.get("ok"):
                        raise WebApiOtaError(
                            f"chunk {seq} rejected: {cr.get('error', cr)}"
                        )
                    last_exc = None
                    break
                except WebApiOtaError:
                    raise   # device-level error — do not retry
                except (_req.exceptions.Timeout,
                        _req.exceptions.ConnectionError) as exc:
                    last_exc = exc
                    self._on_log(
                        f"  [retry {attempt + 1}/{_MAX_RETRIES}] chunk {seq}: {exc}"
                    )

            if last_exc:
                raise WebApiOtaError(
                    f"chunk {seq} failed after {_MAX_RETRIES} attempts: {last_exc}"
                )

            seq    += 1
            offset += len(chunk)

            pct = min(99, seq * 99 // total_chunks)
            self._on_progress(pct)

            if seq % log_every == 0 or seq == total_chunks:
                self._on_log(
                    f"  [{seq:>{w}}/{total_chunks}]"
                    f"  {offset:,}/{fw_size:,} B  ({pct}%)"
                )

        # ── Step 3: complete ──────────────────────────────────────────────────
        self._on_log(f"→ POST /api/ota/complete  (crc32=0x{crc32:08X})")
        try:
            r = sess.post(
                f"{self.base_url}/api/ota/complete",
                json={"crc32": crc32},
                timeout=15,
            )
            r.raise_for_status()
        except Exception as exc:
            raise WebApiOtaError(f"complete request failed: {exc}") from exc

        resp = r.json()
        if not resp.get("ok"):
            raise WebApiOtaError(f"complete rejected: {resp.get('error', resp)}")

        written = resp.get("bytes", fw_size)
        self._on_progress(100)
        self._on_log(f"← OTA complete! {written:,} B written. Device will reboot.")
        self._on_done(True)


# ─────────────────────────────────────────────────────────────────────────────
# CLI entry point
# ─────────────────────────────────────────────────────────────────────────────

def _cli() -> None:
    import sys
    ap = argparse.ArgumentParser(description="FERP OTA over WebAPI (chunked)")
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

