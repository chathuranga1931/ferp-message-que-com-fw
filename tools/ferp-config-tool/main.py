#!/usr/bin/env python3
"""
FERP Config Tool
Light-theme PyQt5 GUI for reading/writing device configuration.

Transports : WebAPI (HTTP POST /api/messages), MQTT, UART (newline-delimited JSON)
Wire format:
  GET  {"msg":"MsgConfigGetKey","data":{"key":<int>}}
       -> {"ok":true,"msg":"MsgConfigValue","data":{"key":N,"type":T,"size":S,"data":[...]}}
  SET  {"msg":"MsgConfigSet","data":{"key":N,"type":T,"size":S,"data":[...]}}
       -> {"ok":true,...}

Types (hsys_type.h):  UINT32=0  STRING=1  BOOL=2
"""

import sys
import json
import struct
import platform
import threading
import queue
import time
from typing import List, Dict, Tuple, Optional
from datetime import datetime
from pathlib import Path

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QComboBox, QLineEdit, QPushButton, QTableWidget, QTableWidgetItem,
    QTextEdit, QSplitter, QGroupBox, QSpinBox, QStackedWidget, QHeaderView,
    QAbstractItemView, QSizePolicy, QFrame, QProgressBar,
)
from PyQt6.QtCore import Qt, QThread, pyqtSignal, QObject
from PyQt6.QtGui import QColor, QFont, QBrush, QTextCursor


# ═══════════════════════════════════════════════════════════════════════════════
# CONFIG KEY DEFINITIONS  (must match app_config.h + hsys_type.h)
# ═══════════════════════════════════════════════════════════════════════════════

TYPE_UINT32 = 0   # HSYS_TYPE_UINT32
TYPE_STRING = 1   # HSYS_TYPE_STRING
TYPE_BOOL   = 2   # HSYS_TYPE_BOOL

TYPE_NAMES: Dict[int, str] = {
    TYPE_UINT32: "UINT32",
    TYPE_STRING: "STRING",
    TYPE_BOOL:   "BOOL",
}

# (key_id, display_name, type)  — types from app_config.h struct field types
CONFIG_KEYS: List[Tuple[int, str, int]] = [
    # WiFi
    (0x1001, "WIFI_SSID",            TYPE_STRING),
    (0x1002, "WIFI_PASSWORD",        TYPE_STRING),
    # Cloud
    (0x2001, "CLOUD_URL",            TYPE_STRING),
    (0x2002, "CLOUD_SECRET",         TYPE_STRING),
    (0x2003, "CLOUD_HB_ENABLED",     TYPE_BOOL),
    (0x2004, "CLOUD_HB_INTERVAL_S",  TYPE_UINT32),
    # OTA
    (0x3001, "OTA_SERVER_URL",       TYPE_STRING),
    (0x3002, "OTA_CHECK_INTERVAL_S", TYPE_UINT32),
    # MQTT
    (0x4001, "MQTT_HOST",            TYPE_STRING),
    (0x4002, "MQTT_PORT",            TYPE_UINT32),
    (0x4003, "MQTT_USER",            TYPE_STRING),
    (0x4004, "MQTT_PASSWORD",        TYPE_STRING),
    # Hardware
    (0x6001, "DISPLAY_TYPE",         TYPE_UINT32),
    (0x6002, "STABILIZE_DELAY_MS",   TYPE_UINT32),
    (0x6003, "EN_RETX",              TYPE_BOOL),
    (0x6004, "NOZZLE_SWAP",          TYPE_BOOL),
    (0x6005, "TOT_CNT",              TYPE_UINT32),
    (0x6006, "TOT_DUR",              TYPE_UINT32),
    # Printer
    (0x7001, "PRINTER_URL",          TYPE_STRING),
    (0x7002, "PRINTER_COPY_COUNT",   TYPE_UINT32),
    (0x7003, "PRINT_DELAY_MS",       TYPE_UINT32),
    # Logging
    (0x8001, "LOG_UDP_ENABLED",      TYPE_BOOL),
    (0x8002, "LOG_UDP_SERVER_IP",    TYPE_STRING),
    (0x8003, "LOG_UDP_PORT",         TYPE_UINT32),
    (0x8004, "DT_LOG_RATE",          TYPE_UINT32),
    # Feature flags
    (0x9001, "ENABLE_NID_PRINT",     TYPE_BOOL),
    (0x9002, "ENABLE_NID_CLOUD",     TYPE_BOOL),
]


# ═══════════════════════════════════════════════════════════════════════════════
# VALUE ENCODE / DECODE
# ═══════════════════════════════════════════════════════════════════════════════

def encode_value(value: str, type_id: int) -> List[int]:
    """Convert user string to byte list for wire format."""
    try:
        if type_id == TYPE_UINT32:
            return list(struct.pack('<I', int(value or 0) & 0xFFFFFFFF))
        elif type_id == TYPE_BOOL:
            v = (value or "").strip().lower() in ('1', 'true', 'yes', 'on')
            return [1 if v else 0]
        else:  # STRING
            return list((value or "").encode('utf-8'))
    except Exception:
        return []


def decode_value(data: List[int], type_id: int) -> str:
    """Convert wire byte list to display string."""
    if not data:
        return ""
    try:
        if type_id == TYPE_UINT32:
            padded = (data + [0, 0, 0, 0])[:4]
            return str(struct.unpack('<I', bytes(padded))[0])
        elif type_id == TYPE_BOOL:
            return "True" if data[0] != 0 else "False"
        else:  # STRING
            return bytes(data).rstrip(b'\x00').decode('utf-8', errors='replace')
    except Exception:
        return ""


# ═══════════════════════════════════════════════════════════════════════════════
# TRANSPORTS
# ═══════════════════════════════════════════════════════════════════════════════

class TransportError(Exception):
    pass


class WebAPITransport:
    def __init__(self, ip: str, port: int = 8080, timeout: float = 3.0):
        try:
            import requests
            self._requests = requests
        except ImportError:
            raise TransportError("requests not installed — run: pip install requests")
        self.url = f"http://{ip.strip()}:{port}/api/messages"
        self.timeout = timeout
        self._session = self._requests.Session()

    def connect(self) -> None:
        # HTTP is stateless — just validate the URL is non-empty and create session.
        # Errors will surface on the first actual read/write.
        if not self.url:
            raise TransportError("No IP address configured")

    def disconnect(self) -> None:
        self._session.close()

    def get_value(self, key: int) -> Tuple[int, List[int]]:
        payload = {"msg": "MsgConfigGetKey", "data": {"key": key}}
        r = self._session.post(self.url, json=payload, timeout=self.timeout)
        r.raise_for_status()
        resp = r.json()
        if not resp.get("ok"):
            raise TransportError(f"Device error for key 0x{key:04X}")
        d = resp.get("data", {})
        return int(d.get("type", 0)), list(d.get("data", []))

    def get_dev_info_value(self, key: int) -> Tuple[bool, str]:
        payload = {"msg": "MsgDevInfoRead", "data": {"key": key, "source_module_id": 0}}
        r = self._session.post(self.url, json=payload, timeout=self.timeout)
        r.raise_for_status()
        resp = r.json()
        if not resp.get("ok"):
            raise TransportError(f"Device error for dev info key 0x{key:04X}")
        d = resp.get("data", {})
        return bool(d.get("is_valid", False)), str(d.get("value", ""))

    def set_value(self, key: int, type_id: int, data: List[int]) -> None:
        payload = {"msg": "MsgConfigSet", "data": {
            "key": key, "type": type_id, "size": len(data), "data": data
        }}
        r = self._session.post(self.url, json=payload, timeout=self.timeout)
        r.raise_for_status()
        resp = r.json()
        if not resp.get("ok"):
            raise TransportError(f"Set rejected for key 0x{key:04X}")


class MQTTTransport:
    def __init__(self, host: str, port: int, cmd_topic: str, evt_topic: str,
                 log_fn=None):
        try:
            import paho.mqtt.client as mqtt_mod
            self._mqtt_mod = mqtt_mod
        except ImportError:
            raise TransportError("paho-mqtt not installed — run: pip install paho-mqtt")
        self.host = host
        self.port = port
        self.cmd_topic = cmd_topic
        self.evt_topic = evt_topic
        self._log_fn = log_fn or (lambda msg, lvl: None)
        self._response_queue: queue.Queue = queue.Queue()
        self._connected = False
        self._client = self._make_client()
        self._client.on_connect    = self._on_connect
        self._client.on_message    = self._on_message
        self._client.on_disconnect = self._on_disconnect

    def _make_client(self):
        # paho-mqtt 2.x requires explicit callback API version
        try:
            from paho.mqtt.enums import CallbackAPIVersion
            return self._mqtt_mod.Client(
                callback_api_version=CallbackAPIVersion.VERSION2
            )
        except ImportError:
            return self._mqtt_mod.Client()

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        if (hasattr(reason_code, 'is_failure') and not reason_code.is_failure) or reason_code == 0:
            self._connected = True
            self._log_fn(f"MQTT connected to {self.host}:{self.port}", "info")
            self._log_fn(f"MQTT SUBSCRIBE  {self.evt_topic}", "info")
            client.subscribe(self.evt_topic)
        else:
            self._log_fn(f"MQTT connect refused: {reason_code}", "error")

    def _on_disconnect(self, client, userdata, flags=None, reason_code=None, properties=None):
        self._connected = False
        self._log_fn(f"MQTT disconnected (reason={reason_code})", "warn")

    def _on_message(self, client, userdata, msg):
        raw = msg.payload.decode('utf-8')
        self._log_fn(f"MQTT RX  [{msg.topic}]  {raw}", "info")
        try:
            self._response_queue.put(json.loads(raw))
        except Exception as exc:
            self._log_fn(f"MQTT RX parse error: {exc}", "error")

    def connect(self) -> None:
        self._log_fn(f"MQTT connecting to {self.host}:{self.port} ...", "info")
        self._client.connect(self.host, self.port, keepalive=60)
        self._client.loop_start()
        deadline = time.time() + 5.0
        while not self._connected and time.time() < deadline:
            time.sleep(0.05)
        if not self._connected:
            raise TransportError(f"MQTT timeout connecting to {self.host}:{self.port}")

    def disconnect(self) -> None:
        self._client.loop_stop()
        self._client.disconnect()
        self._connected = False

    def get_value(self, key: int) -> Tuple[int, List[int]]:
        while not self._response_queue.empty():
            self._response_queue.get_nowait()
        payload = json.dumps({"msg": "MsgConfigGetKey", "data": {"key": key}})
        self._log_fn(f"MQTT TX  [{self.cmd_topic}]  {payload}", "info")
        self._client.publish(self.cmd_topic, payload)
        try:
            resp = self._response_queue.get(timeout=5.0)
        except queue.Empty:
            self._log_fn(f"MQTT response timeout for key 0x{key:04X} — no message on [{self.evt_topic}]", "error")
            raise TransportError(f"MQTT response timeout for key 0x{key:04X}")
        d = resp.get("data", {})
        return int(d.get("type", 0)), list(d.get("data", []))

    def set_value(self, key: int, type_id: int, data: List[int]) -> None:
        payload = json.dumps({"msg": "MsgConfigSet", "data": {
            "key": key, "type": type_id,
            "size": len(data), "data": data
        }})
        self._log_fn(f"MQTT TX  [{self.cmd_topic}]  {payload}", "info")
        self._client.publish(self.cmd_topic, payload)
        time.sleep(0.1)

    def get_dev_info_value(self, key: int) -> Tuple[bool, str]:
        raise TransportError("DevInfo read not supported via MQTT transport")


class UARTTransport:
    def __init__(self, port: str, baud: int):
        try:
            import serial
            self._serial_mod = serial
        except ImportError:
            raise TransportError("pyserial not installed — run: pip install pyserial")
        self.port = port
        self.baud = baud
        self._ser = None
        self._lock = threading.Lock()

    def connect(self) -> None:
        self._ser = self._serial_mod.Serial(self.port, self.baud, timeout=5.0)
        time.sleep(0.3)

    def disconnect(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()

    def _transact(self, payload: dict) -> dict:
        with self._lock:
            self._ser.write((json.dumps(payload) + '\n').encode('utf-8'))
            self._ser.flush()
            deadline = time.time() + 5.0
            while time.time() < deadline:
                raw = self._ser.readline()
                if not raw:
                    continue
                try:
                    resp = json.loads(raw.decode('utf-8').strip())
                    if "ok" in resp or resp.get("msg") in ("MsgConfigValue", "MsgConfigSet"):
                        return resp
                except Exception:
                    pass
            raise TransportError("UART response timeout")

    def get_value(self, key: int) -> Tuple[int, List[int]]:
        resp = self._transact({"msg": "MsgConfigGetKey", "data": {"key": key}})
        d = resp.get("data", {})
        return int(d.get("type", 0)), list(d.get("data", []))

    def set_value(self, key: int, type_id: int, data: List[int]) -> None:
        self._transact({"msg": "MsgConfigSet", "data": {
            "key": key, "type": type_id, "size": len(data), "data": data
        }})

    def get_dev_info_value(self, key: int) -> Tuple[bool, str]:
        raise TransportError("DevInfo read not supported via UART transport")


# ═══════════════════════════════════════════════════════════════════════════════
# SETTINGS & DEVICES
# ═══════════════════════════════════════════════════════════════════════════════

SETTINGS_FILE = Path.home() / ".ferp-config-tool.json"
DEVICES_FILE  = Path(__file__).parent / "ferp_devices.json"
NETWORK_FILE  = Path(__file__).parent / "ferp_network_config.json"


def _load_settings() -> dict:
    try:
        if SETTINGS_FILE.exists():
            return json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
    except Exception:
        pass
    return {}


def _save_settings(data: dict) -> None:
    try:
        SETTINGS_FILE.write_text(json.dumps(data, indent=2), encoding="utf-8")
    except Exception:
        pass


def _load_devices() -> list:
    """Load device list from ferp_devices.json next to main.py."""
    try:
        if DEVICES_FILE.exists():
            return json.loads(DEVICES_FILE.read_text(encoding="utf-8")).get("devices", [])
    except Exception:
        pass
    return []


def _load_network_config() -> dict:
    """Load broker / topic-prefix / port config from ferp_network_config.json."""
    try:
        if NETWORK_FILE.exists():
            return json.loads(NETWORK_FILE.read_text(encoding="utf-8"))
    except Exception:
        pass
    return {}


# Convenience accessors with safe defaults
def _net_mqtt(cfg: dict) -> dict:
    return cfg.get("mqtt", {})

def _net_webapi(cfg: dict) -> dict:
    return cfg.get("webapi", {})


# ═══════════════════════════════════════════════════════════════════════════════
# WORKER THREAD
# ═══════════════════════════════════════════════════════════════════════════════

def _short_err(e: Exception) -> str:
    """Return a concise one-line error, stripping verbose urllib3/requests wrapping."""
    msg = str(e)
    # Extract the innermost parenthesised cause, e.g. "[Errno 65] No route to host"
    import re
    causes = re.findall(r'\(([^()]+)\)', msg)
    if causes:
        return causes[-1].strip()
    # Fallback: truncate at 120 chars
    return msg[:120] + ('...' if len(msg) > 120 else '')

class WorkerSignals(QObject):
    log          = pyqtSignal(str, str)        # (message, level)
    key_read     = pyqtSignal(int, int, list)  # (key, type, data_bytes)
    write_done   = pyqtSignal(int, bool)       # (key, success)
    connected    = pyqtSignal(bool, str)       # (success, message)
    progress     = pyqtSignal(int, int)        # (current, total)
    batch_done   = pyqtSignal()
    dev_info_read = pyqtSignal(int, bool, str) # (key, is_valid, value_str)


class Worker(QThread):
    def __init__(self):
        super().__init__()
        self.signals   = WorkerSignals()
        self._queue: queue.Queue = queue.Queue()
        self._transport = None
        self._running   = True

    def enqueue(self, task: tuple):
        self._queue.put(task)

    def stop(self):
        self._running = False
        self._queue.put(None)

    def run(self):
        while self._running:
            task = self._queue.get()
            if task is None:
                break
            self._process(task)

    def _process(self, task: tuple):
        op = task[0]
        try:
            if op == "connect":
                transport = task[1]
                transport.connect()
                self._transport = transport
                self.signals.connected.emit(True, "Connected")
                self.signals.log.emit("Connected successfully", "info")

            elif op == "disconnect":
                if self._transport:
                    self._transport.disconnect()
                    self._transport = None
                self.signals.connected.emit(False, "Disconnected")
                self.signals.log.emit("Disconnected", "info")

            elif op == "read_key":
                key, hint_type = task[1], task[2]
                if not self._transport:
                    raise TransportError("Not connected")
                self.signals.log.emit(f"READ  0x{key:04X} ...", "info")
                t, data = self._transport.get_value(key)
                self.signals.key_read.emit(key, t, data)
                self.signals.log.emit(f"READ  0x{key:04X} = {decode_value(data, t)!r}  (type={TYPE_NAMES.get(t, t)})", "info")

            elif op == "read_all":
                if not self._transport:
                    raise TransportError("Not connected")
                keys = task[1]
                for i, (key, name, hint_type) in enumerate(keys):
                    self.signals.progress.emit(i, len(keys))
                    try:
                        t, data = self._transport.get_value(key)
                        self.signals.key_read.emit(key, t, data)
                        self.signals.log.emit(
                            f"READ  0x{key:04X}  {name:<24} = {decode_value(data, t)!r}", "info")
                    except Exception as e:
                        self.signals.log.emit(f"READ  0x{key:04X}  {name}  ERROR: {_short_err(e)}", "error")
                        # Abort immediately on network/connection errors — all keys will fail
                        import requests as _req
                        if isinstance(e, (_req.exceptions.ConnectionError,
                                          _req.exceptions.Timeout,
                                          TransportError)):
                            self.signals.log.emit("Aborting batch — device unreachable", "warn")
                            break
                self.signals.progress.emit(len(keys), len(keys))
                self.signals.batch_done.emit()

            elif op == "write_key":
                key, type_id, value_str = task[1], task[2], task[3]
                if not self._transport:
                    raise TransportError("Not connected")
                data = encode_value(value_str, type_id)
                self.signals.log.emit(
                    f"WRITE 0x{key:04X}  [{', '.join(hex(b) for b in data[:8])}{'...' if len(data)>8 else ''}]", "info")
                self._transport.set_value(key, type_id, data)
                self.signals.write_done.emit(key, True)
                self.signals.log.emit(f"WRITE 0x{key:04X} = {value_str!r}  OK", "info")

            elif op == "read_dev_info":
                key = task[1]
                if not self._transport:
                    raise TransportError("Not connected")
                self.signals.log.emit(f"DEV INFO READ 0x{key:04X} ...", "info")
                is_valid, value = self._transport.get_dev_info_value(key)
                self.signals.dev_info_read.emit(key, is_valid, value)
                validity = "" if is_valid else " (not valid)"
                self.signals.log.emit(f"DEV INFO 0x{key:04X} = {value!r}{validity}", "info")

            elif op == "write_all":
                if not self._transport:
                    raise TransportError("Not connected")
                entries = task[1]  # [(key, type_id, value_str), ...]
                for i, (key, type_id, value_str) in enumerate(entries):
                    self.signals.progress.emit(i, len(entries))
                    try:
                        data = encode_value(value_str, type_id)
                        self._transport.set_value(key, type_id, data)
                        self.signals.write_done.emit(key, True)
                        self.signals.log.emit(f"WRITE 0x{key:04X} = {value_str!r}  OK", "info")
                    except Exception as e:
                        self.signals.log.emit(f"WRITE 0x{key:04X}  ERROR: {_short_err(e)}", "error")
                        import requests as _req
                        if isinstance(e, (_req.exceptions.ConnectionError,
                                          _req.exceptions.Timeout,
                                          TransportError)):
                            self.signals.log.emit("Aborting batch — device unreachable", "warn")
                            break
                self.signals.progress.emit(len(entries), len(entries))
                self.signals.batch_done.emit()

        except Exception as e:
            if op == "connect":
                self.signals.connected.emit(False, str(e))
                self.signals.log.emit(f"Connection failed: {e}", "error")
            else:
                self.signals.log.emit(f"Error [{op}]: {_short_err(e)}", "error")


# ═══════════════════════════════════════════════════════════════════════════════
# COLOUR CONSTANTS
# ═══════════════════════════════════════════════════════════════════════════════

CLR_UNLOADED = QColor("#E0E0E0")   # gray   — never read
CLR_LOADED   = QColor("#C8E6C9")   # green  — successfully read from device
CLR_MODIFIED = QColor("#FFE0B2")   # orange — user edited, not yet written

# Table column indices
COL_IDX     = 0
COL_KEYID   = 1
COL_NAME    = 2
COL_TYPE    = 3
COL_VALUE   = 4
COL_REFRESH = 5
COL_WRITE   = 6


# ═══════════════════════════════════════════════════════════════════════════════
# MAIN WINDOW
# ═══════════════════════════════════════════════════════════════════════════════


def _vsep() -> QFrame:
    """Thin vertical separator widget for use inside HBoxLayouts."""
    sep = QFrame()
    sep.setFrameShape(QFrame.Shape.VLine)
    sep.setFrameShadow(QFrame.Shadow.Sunken)
    return sep


class MainWindow(QMainWindow):

    def __init__(self):
        super().__init__()
        self.setWindowTitle("FERP Config Tool")
        self.resize(1150, 860)

        self._connected  = False
        self._key_row: Dict[int, int] = {}
        self._settings      = _load_settings()      # load before _build_ui
        self._devices       = _load_devices()       # load before _build_ui
        self._network_cfg   = _load_network_config() # load before _build_ui

        self._worker = Worker()
        self._worker.signals.log.connect(self._on_log)
        self._worker.signals.key_read.connect(self._on_key_read)
        self._worker.signals.write_done.connect(self._on_write_done)
        self._worker.signals.connected.connect(self._on_connected)
        self._worker.signals.progress.connect(self._on_progress)
        self._worker.signals.batch_done.connect(self._on_batch_done)
        self._worker.signals.dev_info_read.connect(self._on_dev_info_read)
        self._worker.start()

        self._build_ui()
        self._load_settings_to_ui()
        self._apply_style()

    # ── UI Construction ────────────────────────────────────────────────────────

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setSpacing(6)
        root.setContentsMargins(8, 8, 8, 8)

        root.addWidget(self._build_connection_panel())
        root.addWidget(self._build_toolbar())
        root.addWidget(self._build_devinfo_panel())

        splitter = QSplitter(Qt.Orientation.Vertical)
        splitter.addWidget(self._build_table())
        splitter.addWidget(self._build_console())
        splitter.setSizes([420, 280])
        splitter.setCollapsible(0, False)
        splitter.setCollapsible(1, False)
        root.addWidget(splitter, 1)

    # ── Connection Panel ───────────────────────────────────────────────────────

    def _build_connection_panel(self) -> QGroupBox:
        box = QGroupBox("Connection")
        lay = QHBoxLayout(box)
        lay.setSpacing(8)

        lay.addWidget(QLabel("Transport:"))
        self._transport_combo = QComboBox()
        self._transport_combo.addItems(["WebAPI", "MQTT", "UART"])
        self._transport_combo.currentIndexChanged.connect(
            lambda i: self._config_stack.setCurrentIndex(i))
        self._transport_combo.setFixedWidth(90)
        lay.addWidget(self._transport_combo)

        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.VLine)
        sep.setFrameShadow(QFrame.Shadow.Sunken)
        lay.addWidget(sep)

        self._config_stack = QStackedWidget()
        self._config_stack.addWidget(self._build_webapi_panel())
        self._config_stack.addWidget(self._build_mqtt_panel())
        self._config_stack.addWidget(self._build_uart_panel())
        lay.addWidget(self._config_stack, 1)

        sep2 = QFrame()
        sep2.setFrameShape(QFrame.Shape.VLine)
        sep2.setFrameShadow(QFrame.Shadow.Sunken)
        lay.addWidget(sep2)

        self._connect_btn = QPushButton("Connect")
        self._connect_btn.setFixedWidth(90)
        self._connect_btn.clicked.connect(self._on_connect_clicked)
        lay.addWidget(self._connect_btn)

        self._status_dot = QLabel("●")
        self._status_dot.setFont(QFont("Arial", 18))
        self._status_dot.setFixedWidth(26)
        self._set_status_dot(False)
        lay.addWidget(self._status_dot)

        return box

    def _build_webapi_panel(self) -> QWidget:
        w = QWidget()
        lay = QHBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)

        if self._devices:
            lay.addWidget(QLabel("Device:"))
            self._webapi_device = QComboBox()
            self._webapi_device.setFixedWidth(200)
            self._webapi_device.addItem("— select device —", None)
            for d in self._devices:
                self._webapi_device.addItem(d.get("label", d.get("ip", "?")), d)
            self._webapi_device.currentIndexChanged.connect(self._on_device_webapi_selected)
            lay.addWidget(self._webapi_device)
            lay.addWidget(_vsep())
        else:
            self._webapi_device = None

        lay.addWidget(QLabel("IP Address:"))
        self._webapi_ip = QLineEdit("192.168.4.1")
        self._webapi_ip.setPlaceholderText("e.g. 192.168.4.1")
        self._webapi_ip.setFixedWidth(160)
        lay.addWidget(self._webapi_ip)
        lay.addWidget(QLabel("Port:"))
        self._webapi_port = QSpinBox()
        self._webapi_port.setRange(1, 65535)
        _wa_default_port = _net_webapi(self._network_cfg).get("default_port", 8080)
        self._webapi_port.setValue(_wa_default_port)
        self._webapi_port.setFixedWidth(75)
        lay.addWidget(self._webapi_port)
        lay.addStretch()
        return w

    def _build_mqtt_panel(self) -> QWidget:
        w = QWidget()
        lay = QHBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)

        if self._devices:
            lay.addWidget(QLabel("Device:"))
            self._mqtt_device = QComboBox()
            self._mqtt_device.setFixedWidth(200)
            self._mqtt_device.addItem("— select device —", None)
            for d in self._devices:
                self._mqtt_device.addItem(d.get("label", d.get("mac", "?")), d)
            self._mqtt_device.currentIndexChanged.connect(self._on_device_mqtt_selected)
            lay.addWidget(self._mqtt_device)
            lay.addWidget(_vsep())
        else:
            self._mqtt_device = None

        lay.addWidget(QLabel("Broker:"))
        _mqtt_cfg     = _net_mqtt(self._network_cfg)
        _brokers      = _mqtt_cfg.get("brokers", ["localhost"])
        _default_port = int(_mqtt_cfg.get("default_port", 1883))
        _cmd_prefix   = _mqtt_cfg.get("cmd_topic_prefix",  "ferp/ferp-com")
        _resp_prefix  = _mqtt_cfg.get("resp_topic_prefix", "ferp/ferp-com")
        self._mqtt_cmd_prefix  = _cmd_prefix
        self._mqtt_resp_prefix = _resp_prefix
        self._mqtt_host = QComboBox()
        self._mqtt_host.setEditable(True)
        self._mqtt_host.setFixedWidth(160)
        for b in _brokers:
            self._mqtt_host.addItem(b)
        if not _brokers:
            self._mqtt_host.addItem("localhost")
        lay.addWidget(self._mqtt_host)
        lay.addWidget(QLabel("Port:"))
        self._mqtt_port = QSpinBox()
        self._mqtt_port.setRange(1, 65535)
        self._mqtt_port.setValue(_default_port)
        self._mqtt_port.setFixedWidth(70)
        lay.addWidget(self._mqtt_port)
        lay.addWidget(QLabel("Cmd topic:"))
        self._mqtt_cmd = QLineEdit(f"{_cmd_prefix}/cmd")
        self._mqtt_cmd.setFixedWidth(210)
        lay.addWidget(self._mqtt_cmd)
        lay.addWidget(QLabel("Resp topic:"))
        self._mqtt_evt = QLineEdit(f"{_resp_prefix}/resp")
        self._mqtt_evt.setFixedWidth(210)
        lay.addWidget(self._mqtt_evt)
        lay.addStretch()
        return w

    def _build_uart_panel(self) -> QWidget:
        w = QWidget()
        lay = QHBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(QLabel("Port:"))
        default_port = (
            "/dev/tty.usbserial-A5069RR4"
            if platform.system() == "Darwin"
            else ("COM3" if platform.system() == "Windows" else "/dev/ttyUSB0")
        )
        self._uart_port = QLineEdit(default_port)
        self._uart_port.setFixedWidth(220)
        lay.addWidget(self._uart_port)

        scan_btn = QPushButton("Scan")
        scan_btn.setFixedWidth(52)
        scan_btn.setToolTip("List available serial ports")
        scan_btn.clicked.connect(self._on_uart_scan)
        lay.addWidget(scan_btn)

        lay.addWidget(QLabel("Baud:"))
        self._uart_baud = QComboBox()
        for b in [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600]:
            self._uart_baud.addItem(str(b), b)
        self._uart_baud.setCurrentText("115200")
        self._uart_baud.setFixedWidth(100)
        lay.addWidget(self._uart_baud)
        lay.addStretch()
        return w

    # ── Toolbar ────────────────────────────────────────────────────────────────

    def _build_toolbar(self) -> QWidget:
        w = QWidget()
        lay = QHBoxLayout(w)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(8)

        self._read_all_btn = QPushButton("⬇  Read All")
        self._read_all_btn.setFixedHeight(30)
        self._read_all_btn.setEnabled(False)
        self._read_all_btn.clicked.connect(self._on_read_all)
        lay.addWidget(self._read_all_btn)

        self._write_all_btn = QPushButton("⬆  Write All")
        self._write_all_btn.setFixedHeight(30)
        self._write_all_btn.setEnabled(False)
        self._write_all_btn.clicked.connect(self._on_write_all)
        lay.addWidget(self._write_all_btn)

        save_btn = QPushButton("💾  Save Settings")
        save_btn.setFixedHeight(30)
        save_btn.setToolTip(f"Save connection settings to {SETTINGS_FILE}")
        save_btn.clicked.connect(self._on_save_settings)
        lay.addWidget(save_btn)

        lay.addStretch(1)

        self._progress = QProgressBar()
        self._progress.setFixedWidth(200)
        self._progress.setFixedHeight(16)
        self._progress.setVisible(False)
        lay.addWidget(self._progress)

        return w

    # ── Device Info Panel ──────────────────────────────────────────────────────

    # Key constants matching app_device_info.h
    _DEV_INFO_KEYS = [
        (0xA001, "Device UUID",  "device_uuid"),
        (0xA002, "Device Group", "device_group"),
        (0xA003, "HW Address",   "hw_address"),
    ]

    def _build_devinfo_panel(self) -> QGroupBox:
        box = QGroupBox("Device Info")
        lay = QHBoxLayout(box)
        lay.setSpacing(12)
        lay.setContentsMargins(8, 4, 8, 4)

        self._devinfo_fields: Dict[int, QLineEdit] = {}
        self._devinfo_btns:   Dict[int, QPushButton] = {}

        for key, label, _ in self._DEV_INFO_KEYS:
            lay.addWidget(QLabel(f"{label}:"))
            edit = QLineEdit()
            edit.setReadOnly(True)
            edit.setPlaceholderText("—")
            edit.setMinimumWidth(200)
            self._devinfo_fields[key] = edit
            lay.addWidget(edit, 1)

            btn = QPushButton("↺")
            btn.setFixedSize(28, 22)
            btn.setToolTip(f"Read {label} from device")
            btn.setEnabled(False)
            btn.clicked.connect(lambda _, k=key: self._on_read_dev_info_key(k))
            self._devinfo_btns[key] = btn
            lay.addWidget(btn)

        ref_all = QPushButton("↺ All")
        ref_all.setFixedHeight(24)
        ref_all.setToolTip("Read all device info fields from device")
        ref_all.setEnabled(False)
        ref_all.clicked.connect(self._on_read_all_dev_info)
        self._devinfo_refresh_all_btn = ref_all
        lay.addWidget(ref_all)

        return box

    # ── Config Table ───────────────────────────────────────────────────────────

    def _build_table(self) -> QTableWidget:
        self._table = QTableWidget(len(CONFIG_KEYS), 7)
        self._table.setHorizontalHeaderLabels(
            ["#", "Key ID", "Key Name", "Type", "Value", "↺", "✎"]
        )
        self._table.verticalHeader().setVisible(False)
        self._table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self._table.setEditTriggers(
            QAbstractItemView.EditTrigger.DoubleClicked | QAbstractItemView.EditTrigger.AnyKeyPressed
        )
        self._table.setAlternatingRowColors(True)
        self._table.setShowGrid(True)

        hdr = self._table.horizontalHeader()
        hdr.setSectionResizeMode(COL_IDX,     QHeaderView.ResizeMode.Fixed)
        hdr.setSectionResizeMode(COL_KEYID,   QHeaderView.ResizeMode.Fixed)
        hdr.setSectionResizeMode(COL_NAME,    QHeaderView.ResizeMode.ResizeToContents)
        hdr.setSectionResizeMode(COL_TYPE,    QHeaderView.ResizeMode.Fixed)
        hdr.setSectionResizeMode(COL_VALUE,   QHeaderView.ResizeMode.Stretch)
        hdr.setSectionResizeMode(COL_REFRESH, QHeaderView.ResizeMode.Fixed)
        hdr.setSectionResizeMode(COL_WRITE,   QHeaderView.ResizeMode.Fixed)

        self._table.setColumnWidth(COL_IDX,     32)
        self._table.setColumnWidth(COL_KEYID,   70)
        self._table.setColumnWidth(COL_TYPE,    65)
        self._table.setColumnWidth(COL_REFRESH, 36)
        self._table.setColumnWidth(COL_WRITE,   36)

        for row, (key, name, type_id) in enumerate(CONFIG_KEYS):
            self._key_row[key] = row
            self._table.setRowHeight(row, 26)

            def _ro(text, align=Qt.AlignmentFlag.AlignCenter):
                it = QTableWidgetItem(text)
                it.setFlags(Qt.ItemFlag.ItemIsEnabled | Qt.ItemFlag.ItemIsSelectable)
                it.setTextAlignment(align)
                return it

            self._table.setItem(row, COL_IDX,   _ro(str(row + 1)))
            self._table.setItem(row, COL_KEYID, _ro(f"0x{key:04X}"))
            self._table.setItem(row, COL_NAME,  _ro(name, Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter))
            self._table.setItem(row, COL_TYPE,  _ro(TYPE_NAMES[type_id]))

            val_item = QTableWidgetItem("")
            val_item.setBackground(QBrush(CLR_UNLOADED))
            self._table.setItem(row, COL_VALUE, val_item)

            # Per-row Refresh button
            rb = QPushButton("↺")
            rb.setFixedSize(28, 22)
            rb.setToolTip(f"Read {name} from device")
            rb.clicked.connect(lambda _, k=key, t=type_id: self._on_read_key(k, t))
            self._table.setCellWidget(row, COL_REFRESH, self._wrap_center(rb))

            # Per-row Write button
            wb = QPushButton("✎")
            wb.setFixedSize(28, 22)
            wb.setToolTip(f"Write {name} to device")
            wb.clicked.connect(lambda _, r=row, k=key, t=type_id: self._on_write_key(r, k, t))
            self._table.setCellWidget(row, COL_WRITE, self._wrap_center(wb))

        self._table.itemChanged.connect(self._on_cell_changed)
        return self._table

    @staticmethod
    def _wrap_center(widget: QWidget) -> QWidget:
        c = QWidget()
        lay = QHBoxLayout(c)
        lay.setContentsMargins(2, 1, 2, 1)
        lay.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(widget)
        return c

    # ── Console ────────────────────────────────────────────────────────────────

    def _build_console(self) -> QGroupBox:
        box = QGroupBox("Console")
        lay = QVBoxLayout(box)
        lay.setContentsMargins(4, 4, 4, 4)

        self._console = QTextEdit()
        self._console.setReadOnly(True)
        self._console.setFont(QFont("Courier New", 10))
        lay.addWidget(self._console)

        btn_row = QHBoxLayout()
        btn_row.addStretch()
        clr = QPushButton("Clear")
        clr.setFixedWidth(60)
        clr.clicked.connect(self._console.clear)
        btn_row.addWidget(clr)
        lay.addLayout(btn_row)
        return box

    # ── Styling ────────────────────────────────────────────────────────────────

    def _apply_style(self):
        self.setStyleSheet("""
            QMainWindow, QWidget { background: #FAFAFA; color: #212121; }

            QGroupBox {
                border: 1px solid #BDBDBD; border-radius: 4px;
                margin-top: 10px; font-weight: bold;
            }
            QGroupBox::title {
                subcontrol-origin: margin; left: 10px; padding: 0 4px;
            }

            QTableWidget {
                border: 1px solid #BDBDBD; gridline-color: #E0E0E0;
                background: #FFFFFF; alternate-background-color: #F7F7F7;
                selection-background-color: #BBDEFB; selection-color: #212121;
            }
            QHeaderView::section {
                background: #EEEEEE; font-weight: bold;
                border: none; border-right: 1px solid #BDBDBD;
                border-bottom: 1px solid #BDBDBD; padding: 3px 5px;
            }

            QPushButton {
                background: #E3F2FD; border: 1px solid #90CAF9;
                border-radius: 3px; padding: 4px 10px; color: #1565C0;
            }
            QPushButton:hover   { background: #BBDEFB; }
            QPushButton:pressed { background: #90CAF9; }
            QPushButton:disabled {
                background: #F5F5F5; color: #9E9E9E; border-color: #E0E0E0;
            }

            QLineEdit, QSpinBox, QComboBox {
                background: #FFFFFF; border: 1px solid #BDBDBD;
                border-radius: 3px; padding: 3px 5px; color: #212121;
            }
            QLineEdit:focus, QSpinBox:focus { border-color: #1976D2; }

            QTextEdit {
                background: #FFFFFF; border: 1px solid #BDBDBD;
            }

            QProgressBar {
                border: 1px solid #BDBDBD; border-radius: 3px;
                background: #E0E0E0; text-align: center;
            }
            QProgressBar::chunk { background: #42A5F5; border-radius: 3px; }

            QSplitter::handle { background: #E0E0E0; }
        """)

    # ── Status indicator ───────────────────────────────────────────────────────

    def _set_status_dot(self, connected: bool):
        color = "#4CAF50" if connected else "#9E9E9E"
        self._status_dot.setStyleSheet(f"color: {color};")

    # ── Slots — connection ─────────────────────────────────────────────────────

    def _on_connect_clicked(self):
        if self._connected:
            self._worker.enqueue(("disconnect",))
            return

        idx = self._transport_combo.currentIndex()
        try:
            if idx == 0:
                transport = WebAPITransport(self._webapi_ip.text(), self._webapi_port.value())
            elif idx == 1:
                transport = MQTTTransport(
                    self._mqtt_host.currentText(), self._mqtt_port.value(),
                    self._mqtt_cmd.text(),  self._mqtt_evt.text(),
                    log_fn=self._worker.signals.log.emit
                )
            else:
                transport = UARTTransport(
                    self._uart_port.text(), self._uart_baud.currentData()
                )
        except TransportError as e:
            self._log(str(e), "error")
            return

        self._connect_btn.setEnabled(False)
        self._log(f"Connecting via {self._transport_combo.currentText()} ...", "info")
        self._worker.enqueue(("connect", transport))

    def _on_connected(self, success: bool, msg: str):
        self._connect_btn.setEnabled(True)
        self._connected = success
        self._set_status_dot(success)
        if success:
            self._connect_btn.setText("Disconnect")
            self._read_all_btn.setEnabled(True)
            self._write_all_btn.setEnabled(True)
        else:
            self._connect_btn.setText("Connect")
            self._read_all_btn.setEnabled(False)
            self._write_all_btn.setEnabled(False)
        for btn in self._devinfo_btns.values():
            btn.setEnabled(success)
        self._devinfo_refresh_all_btn.setEnabled(success)
        if not success:
            for edit in self._devinfo_fields.values():
                edit.clear()
                edit.setPlaceholderText("—")

    def _on_uart_scan(self):
        try:
            import serial.tools.list_ports
            ports = [p.device for p in serial.tools.list_ports.comports()]
            if ports:
                self._uart_port.setText(ports[0])
                self._log("Available ports: " + ", ".join(ports), "info")
            else:
                self._log("No serial ports found", "warn")
        except ImportError:
            self._log("pyserial not installed", "error")

    # ── Slots — read / write ───────────────────────────────────────────────────

    def _on_read_all(self):
        if not self._connected:
            return
        self._read_all_btn.setEnabled(False)
        self._write_all_btn.setEnabled(False)
        self._progress.setVisible(True)
        self._worker.enqueue(("read_all", CONFIG_KEYS))

    def _on_write_all(self):
        if not self._connected:
            return
        entries = []
        for row, (key, name, type_id) in enumerate(CONFIG_KEYS):
            it = self._table.item(row, COL_VALUE)
            if it:
                entries.append((key, type_id, it.text()))
        self._read_all_btn.setEnabled(False)
        self._write_all_btn.setEnabled(False)
        self._progress.setVisible(True)
        self._worker.enqueue(("write_all", entries))

    def _on_read_key(self, key: int, type_id: int):
        if not self._connected:
            self._log("Not connected", "warn")
            return
        self._worker.enqueue(("read_key", key, type_id))

    def _on_write_key(self, row: int, key: int, type_id: int):
        if not self._connected:
            self._log("Not connected", "warn")
            return
        it = self._table.item(row, COL_VALUE)
        if it:
            self._worker.enqueue(("write_key", key, type_id, it.text()))

    # ── Slots — data update ────────────────────────────────────────────────────

    def _on_key_read(self, key: int, type_id: int, data: list):
        row = self._key_row.get(key)
        if row is None:
            return
        value_str = decode_value(data, type_id)
        self._table.blockSignals(True)
        it = self._table.item(row, COL_VALUE)
        if it:
            it.setText(value_str)
            it.setBackground(QBrush(CLR_LOADED))
            # Also update type column with actual type from device
            type_it = self._table.item(row, COL_TYPE)
            if type_it:
                type_it.setText(TYPE_NAMES.get(type_id, str(type_id)))
        self._table.blockSignals(False)

    def _on_write_done(self, key: int, success: bool):
        row = self._key_row.get(key)
        if row is None:
            return
        if success:
            self._table.blockSignals(True)
            it = self._table.item(row, COL_VALUE)
            if it:
                it.setBackground(QBrush(CLR_LOADED))
            self._table.blockSignals(False)

    def _on_cell_changed(self, item: QTableWidgetItem):
        """Mark a cell orange when the user edits it manually."""
        if item.column() != COL_VALUE:
            return
        self._table.blockSignals(True)
        item.setBackground(QBrush(CLR_MODIFIED))
        self._table.blockSignals(False)

    def _on_batch_done(self):
        if self._connected:
            self._read_all_btn.setEnabled(True)
            self._write_all_btn.setEnabled(True)
        self._progress.setVisible(False)

    def _on_progress(self, current: int, total: int):
        if total == 0:
            return
        if current >= total:
            self._progress.setVisible(False)
        else:
            self._progress.setMaximum(total)
            self._progress.setValue(current)

    # ── Console logger ─────────────────────────────────────────────────────────

    def _on_log(self, msg: str, level: str):
        self._log(msg, level)

    # ── Settings persist ─────────────────────────────────────────────────────────────

    def _load_settings_to_ui(self):
        s = self._settings
        if not s:
            return
        idx = int(s.get("transport", 0))
        self._transport_combo.setCurrentIndex(idx)
        self._config_stack.setCurrentIndex(idx)
        # WebAPI
        if s.get("webapi_ip"):
            self._webapi_ip.setText(s["webapi_ip"])
        if s.get("webapi_port"):
            self._webapi_port.setValue(int(s["webapi_port"]))
        # MQTT
        if s.get("mqtt_host"):
            self._mqtt_host.setCurrentText(s["mqtt_host"])
        if s.get("mqtt_port"):
            self._mqtt_port.setValue(int(s["mqtt_port"]))
        if s.get("mqtt_cmd"):
            self._mqtt_cmd.setText(s["mqtt_cmd"])
        if s.get("mqtt_evt"):
            self._mqtt_evt.setText(s["mqtt_evt"])
        # UART
        if s.get("uart_port"):
            self._uart_port.setText(s["uart_port"])
        if s.get("uart_baud"):
            self._uart_baud.setCurrentText(str(s["uart_baud"]))

    def _collect_settings(self) -> dict:
        return {
            "transport":   self._transport_combo.currentIndex(),
            "webapi_ip":   self._webapi_ip.text(),
            "webapi_port": self._webapi_port.value(),
            "mqtt_host":   self._mqtt_host.currentText(),
            "mqtt_port":   self._mqtt_port.value(),
            "mqtt_cmd":    self._mqtt_cmd.text(),
            "mqtt_evt":    self._mqtt_evt.text(),
            "uart_port":   self._uart_port.text(),
            "uart_baud":   self._uart_baud.currentText(),
        }

    def _on_save_settings(self):
        _save_settings(self._collect_settings())
        self._log(f"Settings saved → {SETTINGS_FILE}", "info")

    # ── Device selection ────────────────────────────────────────────────────────────

    # Mapping from ferp_devices.json field → DevInfo key (app_device_info.h)
    _DEVINFO_FROM_DEVICE = {
        0xA001: "uuid",   # Device UUID
        0xA002: "group",  # Device Group
        0xA003: "mac",    # HW Address
    }

    def _apply_device_to_devinfo(self, d: dict):
        """Populate Device Info panel fields from a device dict entry."""
        for key, field in self._DEVINFO_FROM_DEVICE.items():
            edit = self._devinfo_fields.get(key)
            if edit is None:
                continue
            value = d.get(field, "")
            edit.setText(value)
            edit.setPlaceholderText("—" if value else "(not set)")

    def _on_device_webapi_selected(self, idx: int):
        if self._webapi_device is None:
            return
        d = self._webapi_device.itemData(idx)
        if d is None:
            return
        ip = d.get("ip", "")
        if ip:
            self._webapi_ip.setText(ip)
        self._apply_device_to_devinfo(d)
        self._log(f"Device selected: {d.get('label', ip or '?')}  (IP {ip})", "info")

    def _on_device_mqtt_selected(self, idx: int):
        if self._mqtt_device is None:
            return
        d = self._mqtt_device.itemData(idx)
        if d is None:
            return
        mac   = d.get("mac", "")
        uuid_ = d.get("uuid", "")
        group = d.get("group", "default")
        dev_id = uuid_ if uuid_ else mac
        if dev_id:
            self._mqtt_cmd.setText(f"{self._mqtt_cmd_prefix}/{group}/{dev_id}/cmd")
            self._mqtt_evt.setText(f"{self._mqtt_resp_prefix}/{group}/{dev_id}/resp")
        self._apply_device_to_devinfo(d)
        self._log(f"Device selected: {d.get('label', dev_id or '?')}  (ID {dev_id})", "info")

    # ── Slots — device info ────────────────────────────────────────────────────

    def _on_read_dev_info_key(self, key: int):
        if not self._connected:
            self._log("Not connected", "warn")
            return
        self._worker.enqueue(("read_dev_info", key))

    def _on_read_all_dev_info(self):
        if not self._connected:
            return
        for key, _, _ in self._DEV_INFO_KEYS:
            self._worker.enqueue(("read_dev_info", key))

    def _on_dev_info_read(self, key: int, is_valid: bool, value: str):
        edit = self._devinfo_fields.get(key)
        if edit is None:
            return
        edit.setText(value if is_valid else "")
        edit.setPlaceholderText("—" if is_valid else "(not set)")

    def _log(self, msg: str, level: str = "info"):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        color = {"info": "#1565C0", "warn": "#E65100", "error": "#B71C1C"}.get(level, "#212121")
        self._console.append(
            f'<span style="color:#757575">[{ts}]</span>'
            f' <span style="color:{color}">{msg}</span>'
        )
        self._console.moveCursor(QTextCursor.MoveOperation.End)

    # ── Lifecycle ──────────────────────────────────────────────────────────────

    def closeEvent(self, event):
        _save_settings(self._collect_settings())
        self._worker.stop()
        self._worker.wait(2000)
        event.accept()


# ═══════════════════════════════════════════════════════════════════════════════
# ENTRY POINT
# ═══════════════════════════════════════════════════════════════════════════════

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = MainWindow()
    win.show()
    sys.exit(app.exec())
