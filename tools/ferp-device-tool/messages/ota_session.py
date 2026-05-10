"""
ota_session.py — Shared OTA firmware-update session logic.

Used by both ferp_mqtt_ota.py (CLI) and ferp_mqtt_gui.py (GUI OTA tab).

Protocol topics:
    .../ota/ctrl  — JSON control commands  (ota_start, ota_abort, ota_complete)
    .../ota/data  — Binary chunks          [OFFSET 4B big-endian][DATA]
    .../ota/resp  — JSON responses from device

Usage
-----
    session = OtaSession(
        broker="192.168.1.1", port=1883,
        dev_type="ferp-com", group="default", device_id="<uuid>",
        firmware_path="/path/to/fw.bin",
        target="main", version="1.2.3",
        on_log=print,
        on_progress=lambda pct: ...,
        on_done=lambda ok: ...,
    )
    session.start()    # runs in background thread

    # To abort mid-flash:
    session.abort()

Callbacks are invoked from the background thread — GUI callers must
schedule any UI updates with  widget.after(0, ...).
"""

import json
import os
import random
import struct
import threading
import zlib

import paho.mqtt.client as mqtt


# ---------------------------------------------------------------------------
# CRC32 (matches ESP-IDF esp_crc32_le — standard zlib polynomial)
# ---------------------------------------------------------------------------

def crc32_of_file(path: str) -> int:
    crc = 0
    with open(path, "rb") as fh:
        while chunk := fh.read(65536):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFF_FFFF


# ---------------------------------------------------------------------------
# Topic helpers
# ---------------------------------------------------------------------------

def _base(dev_type: str, group: str, device_id: str) -> str:
    safe_id = device_id.replace(":", "").replace("-", "").lower()
    return f"ferp/{dev_type}/{group}/{safe_id}"

def _ota_ctrl(base: str) -> str: return f"{base}/ota/ctrl"
def _ota_data(base: str) -> str: return f"{base}/ota/data"
def _ota_resp(base: str) -> str: return f"{base}/ota/resp"


# ---------------------------------------------------------------------------
# OtaSession
# ---------------------------------------------------------------------------

class OtaSession:
    """
    Manages a complete OTA firmware-update session over MQTT.

    Parameters
    ----------
    broker, port          : MQTT broker connection details
    dev_type, group,
    device_id             : FERP device addressing
    firmware_path         : absolute path to the .bin firmware file
    target                : "main" | "sub1"
    version               : firmware version string (informational)
    chunk_size            : bytes per chunk (default 4096)
    ctrl_timeout          : seconds to wait for ctrl responses (default 15)
    chunk_timeout         : seconds to wait for chunk ack (default 10)
    max_retries           : max chunk retries before abort (default 3)
    on_log(msg: str)      : called with status/log messages
    on_progress(pct: int) : called with 0-100 progress
    on_done(ok: bool)     : called when session ends (True = success)
    """

    def __init__(
        self,
        broker: str,
        port: int,
        dev_type: str,
        group: str,
        device_id: str,
        firmware_path: str,
        target: str,
        version: str = "unknown",
        chunk_size: int = 4096,
        ctrl_timeout: float = 15.0,
        chunk_timeout: float = 10.0,
        max_retries: int = 3,
        on_log=None,
        on_progress=None,
        on_done=None,
    ):
        self.broker        = broker
        self.port          = port
        self.base          = _base(dev_type, group, device_id)
        self.firmware_path = firmware_path
        self.target        = target
        self.version       = version
        self.chunk_size    = chunk_size
        self.ctrl_timeout  = ctrl_timeout
        self.chunk_timeout = chunk_timeout
        self.max_retries   = max_retries

        self.on_log      = on_log      or (lambda m: None)
        self.on_progress = on_progress or (lambda p: None)
        self.on_done     = on_done     or (lambda ok: None)

        self._abort_flag  = threading.Event()
        self._ctrl_evt   = threading.Event()
        self._chunk_evt  = threading.Event()
        self._connected  = threading.Event()
        self._subscribed = threading.Event()   # set after SUBACK for ota/resp
        self._last_resp: dict | None = None
        self._offset_next = 0
        self._client: mqtt.Client | None = None

    # ── Public API ────────────────────────────────────────────────────────────

    def start(self) -> None:
        """Launch the OTA session in a daemon background thread."""
        t = threading.Thread(target=self._run, daemon=True)
        t.start()

    def abort(self) -> None:
        """Request a graceful abort.  The background thread will stop soon."""
        self._abort_flag.set()

    # ── MQTT callbacks ────────────────────────────────────────────────────────

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            # Subscribe first — wait for SUBACK before signalling _connected
            # so we never miss the ota_start response.
            client.subscribe(_ota_resp(self.base), qos=1)
        else:
            self.on_log(f"[error] MQTT connect refused (rc={rc})")

    def _on_subscribe(self, client, userdata, mid, granted_qos):
        """Called when SUBACK arrives — subscription is now active."""
        self._connected.set()
        self._subscribed.set()

    def _on_message(self, client, userdata, message):
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception as exc:
            self.on_log(f"[warn] Bad JSON on {message.topic}: {exc}")
            return
        self._last_resp = payload
        cmd = payload.get("cmd", "")
        if cmd in ("ota_start", "ota_abort", "ota_status", "ota_complete"):
            self.on_log(f"[dbg]  ctrl resp: {json.dumps(payload)}")
            self._ctrl_evt.set()
        elif cmd == "ota_chunk":
            self._offset_next = payload.get("offset_next", 0)
            self._chunk_evt.set()
        else:
            # Unknown cmd — still unblock ctrl waiter (device might use slightly
            # different field names); log it so the user can diagnose.
            self.on_log(f"[warn] Unexpected resp cmd={cmd!r} — unblocking ctrl wait")
            self._ctrl_evt.set()

    # ── Internal helpers ──────────────────────────────────────────────────────

    def _send_ctrl(self, payload: dict) -> dict | None:
        self._ctrl_evt.clear()
        self._last_resp = None
        self._client.publish(_ota_ctrl(self.base), json.dumps(payload), qos=1)
        if not self._ctrl_evt.wait(timeout=self.ctrl_timeout):
            return None
        return self._last_resp

    def _send_chunk(self, offset: int, data: bytes) -> dict | None:
        self._chunk_evt.clear()
        self._last_resp = None
        frame = struct.pack(">I", offset) + data
        self._client.publish(_ota_data(self.base), frame, qos=1)
        if not self._chunk_evt.wait(timeout=self.chunk_timeout):
            return None
        return self._last_resp

    def _send_abort(self) -> None:
        if self._client:
            self._client.publish(
                _ota_ctrl(self.base),
                json.dumps({"seq": 99, "cmd": "ota_abort"}),
                qos=1,
            )

    # ── Main session flow ─────────────────────────────────────────────────────

    def _run(self) -> None:
        if not os.path.isfile(self.firmware_path):
            self.on_log(f"[error] Firmware file not found: {self.firmware_path}")
            self.on_done(False)
            return

        fw_size = os.path.getsize(self.firmware_path)
        fw_crc  = crc32_of_file(self.firmware_path)
        self.on_log(
            f"Firmware : {os.path.basename(self.firmware_path)}\n"
            f"Size     : {fw_size:,} bytes\n"
            f"CRC32    : 0x{fw_crc:08X}\n"
            f"Target   : {self.target}  version={self.version}"
        )

        cid = f"ferp-ota-{random.randint(1000, 9999)}"
        self._client = mqtt.Client(client_id=cid)
        self._client.on_connect   = self._on_connect
        self._client.on_subscribe = self._on_subscribe
        self._client.on_message   = self._on_message

        self.on_log(f"Connecting to {self.broker}:{self.port} ...")
        try:
            self._client.connect(self.broker, self.port, keepalive=60)
        except Exception as exc:
            self.on_log(f"[error] Connect failed: {exc}")
            self.on_done(False)
            return

        self._client.loop_start()

        # Wait for both CONNACK and SUBACK — subscription happens in _on_connect.
        if not self._connected.wait(timeout=15.0):
            self.on_log("[error] Broker/subscribe timeout (15 s)")
            self._client.loop_stop()
            self.on_done(False)
            return

        self.on_log(f"Connected. OTA resp: {_ota_resp(self.base)}")

        # ── 1. ota_start ──────────────────────────────────────────────────────
        self.on_log("→ ota_start …")
        resp = self._send_ctrl({
            "seq": 1,
            "cmd": "ota_start",
            "data": {
                "target":  self.target,
                "size":    fw_size,
                "version": self.version,
                "crc32":   f"0x{fw_crc:08X}",
            },
        })
        if resp is None or resp.get("status") != "ok":
            code = (resp or {}).get("code", "no response")
            self.on_log(f"[error] ota_start rejected: {code}")
            self._client.loop_stop()
            self.on_done(False)
            return
        self.on_log("← ota_start accepted")

        # ── 2. Stream chunks ──────────────────────────────────────────────────
        # Use an explicit offset (not a chunk-index counter) so that resyncs
        # correctly realign both the file position and the offset in one place.
        sent_bytes = 0
        offset     = 0

        with open(self.firmware_path, "rb") as fh:
            while offset < fw_size:
                if self._abort_flag.is_set():
                    self.on_log("Abort requested — sending ota_abort")
                    self._send_abort()
                    self._client.loop_stop()
                    self.on_done(False)
                    return

                fh.seek(offset)
                chunk        = fh.read(self.chunk_size)
                if not chunk:
                    break
                retries_left = self.max_retries

                while True:
                    resp = self._send_chunk(offset, chunk)

                    if resp is None:
                        retries_left -= 1
                        if retries_left < 0:
                            self.on_log(f"[error] Chunk timeout at offset {offset}")
                            self._send_abort()
                            self._client.loop_stop()
                            self.on_done(False)
                            return
                        self.on_log(
                            f"[warn] Chunk timeout at offset {offset} — "
                            f"retry ({retries_left} left)"
                        )
                        continue

                    if resp.get("status") != "ok":
                        # Device signals the offset it actually expects next.
                        expected = resp.get("offset_expecting", offset)
                        self.on_log(f"[warn] Resync to offset {expected}")
                        fh.seek(expected)
                        offset = expected
                        chunk  = fh.read(self.chunk_size)
                        if not chunk:
                            break
                        continue

                    # Validate that the device wrote the full chunk.
                    # If offset_next < offset + len(chunk) the device only wrote
                    # a partial chunk (e.g. due to MQTT buffer fragmentation).
                    # Treat this as a resync to offset_next so we re-send the
                    # missing data.
                    offset_next = resp.get("offset_next", offset + len(chunk))
                    if offset_next != offset + len(chunk):
                        self.on_log(
                            f"[warn] Partial chunk at offset {offset}: "
                            f"expected offset_next={offset + len(chunk)}, "
                            f"got {offset_next} — resyncing"
                        )
                        fh.seek(offset_next)
                        offset = offset_next
                        chunk  = fh.read(self.chunk_size)
                        if not chunk:
                            break
                        continue

                    break   # chunk fully accepted

                sent_bytes += len(chunk)
                offset     += len(chunk)
                self.on_progress(int(sent_bytes * 100 / fw_size))

        # ── 3. ota_complete ───────────────────────────────────────────────────
        self.on_log("→ ota_complete …")
        resp = self._send_ctrl({
            "seq": 3,
            "cmd": "ota_complete",
            "data": {"crc32": f"0x{fw_crc:08X}"},
        })
        if resp is None or resp.get("status") != "ok":
            code = (resp or {}).get("code", "no response")
            self.on_log(f"[error] ota_complete failed: {code}")
            self._client.loop_stop()
            self.on_done(False)
            return

        self.on_progress(100)
        self.on_log("← OTA complete! Device will reboot.")
        self._client.loop_stop()
        self._client.disconnect()
        self.on_done(True)
