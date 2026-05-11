"""
transports.py — Transport backends for FERP Device Tool.

Provides WebAPITransport, MQTTTransport, and UARTTransport.
All expose the same interface:
    connect()  / disconnect()
    get_value(key) -> (type_id, data_bytes)
    set_value(key, type_id, data_bytes)
    get_dev_info_value(key) -> (is_valid, value_str)

For MQTT raw-messaging (FERP MQTT Tool mode) the MqttRawClient class
is a lower-level wrapper used by the MessageTree panel.
"""

import json
import queue
import random
import struct
import threading
import time
from typing import List, Tuple


# ─────────────────────────────────────────────────────────────────────────────
# Type constants (must match hsys_type.h)
# ─────────────────────────────────────────────────────────────────────────────

TYPE_UINT32 = 0
TYPE_STRING = 1
TYPE_BOOL   = 2

TYPE_NAMES = {
    TYPE_UINT32: "UINT32",
    TYPE_STRING: "STRING",
    TYPE_BOOL:   "BOOL",
}

# Reverse mapping used when loading from JSON
_TYPE_NAME_TO_ID = {v: k for k, v in TYPE_NAMES.items()}


def type_id_from_name(name: str) -> int:
    """'STRING' -> 1  (falls back to TYPE_STRING if unknown)"""
    return _TYPE_NAME_TO_ID.get(name.upper(), TYPE_STRING)


# ─────────────────────────────────────────────────────────────────────────────
# Encode / decode helpers
# ─────────────────────────────────────────────────────────────────────────────

def encode_value(value: str, type_id: int) -> List[int]:
    try:
        if type_id == TYPE_UINT32:
            return list(struct.pack('<I', int(value or 0) & 0xFFFFFFFF))
        elif type_id == TYPE_BOOL:
            v = (value or "").strip().lower() in ('1', 'true', 'yes', 'on')
            return [1 if v else 0]
        else:
            return list((value or "").encode('utf-8'))
    except Exception:
        return []


def decode_value(data: List[int], type_id: int) -> str:
    if not data:
        return ""
    try:
        if type_id == TYPE_UINT32:
            padded = (data + [0, 0, 0, 0])[:4]
            return str(struct.unpack('<I', bytes(padded))[0])
        elif type_id == TYPE_BOOL:
            return "True" if data[0] != 0 else "False"
        else:
            return bytes(data).rstrip(b'\x00').decode('utf-8', errors='replace')
    except Exception:
        return ""


# ─────────────────────────────────────────────────────────────────────────────
# Base exception
# ─────────────────────────────────────────────────────────────────────────────

class TransportError(Exception):
    pass


# ─────────────────────────────────────────────────────────────────────────────
# WebAPI Transport
# ─────────────────────────────────────────────────────────────────────────────

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

    def set_value(self, key: int, type_id: int, data: List[int]) -> None:
        payload = {"msg": "MsgConfigSet", "data": {
            "key": key, "type": type_id, "size": len(data), "data": data
        }}
        r = self._session.post(self.url, json=payload, timeout=self.timeout)
        r.raise_for_status()
        resp = r.json()
        if not resp.get("ok"):
            raise TransportError(f"Set rejected for key 0x{key:04X}")

    def get_dev_info_value(self, key: int) -> Tuple[bool, str]:
        payload = {"msg": "MsgDevInfoRead", "data": {"key": key, "source_module_id": 0}}
        r = self._session.post(self.url, json=payload, timeout=self.timeout)
        r.raise_for_status()
        resp = r.json()
        if not resp.get("ok"):
            raise TransportError(f"Device error for dev info key 0x{key:04X}")
        d = resp.get("data", {})
        return bool(d.get("is_valid", False)), str(d.get("value", ""))

    def send_message(self, msg_name: str, data: dict) -> dict:
        """Send an arbitrary message via POST /api/messages and return the response body."""
        payload = {"msg": msg_name, "data": data}
        r = self._session.post(self.url, json=payload, timeout=self.timeout)
        r.raise_for_status()
        resp = r.json()
        if not resp.get("ok"):
            raise TransportError(
                f"Device rejected '{msg_name}': {resp.get('error', resp)}"
            )
        return resp


# ─────────────────────────────────────────────────────────────────────────────
# MQTT Config/DevInfo Transport
# ─────────────────────────────────────────────────────────────────────────────

class MQTTTransport:
    """
    MQTT transport for config read/write and dev-info read.
    cmd_topic  — topic the device subscribes to (commands go here)
    resp_topic — topic the device publishes responses on
    """

    def __init__(self, host: str, port: int, cmd_topic: str, resp_topic: str,
                 log_fn=None):
        try:
            import paho.mqtt.client as mqtt_mod
            self._mqtt_mod = mqtt_mod
        except ImportError:
            raise TransportError("paho-mqtt not installed — run: pip install paho-mqtt")
        self.host       = host
        self.port       = port
        self.cmd_topic  = cmd_topic
        self.resp_topic = resp_topic
        self._log       = log_fn or (lambda msg, lvl: None)
        self._queue: queue.Queue = queue.Queue()
        self._connected = False
        self._client    = self._make_client()
        self._client.on_connect    = self._on_connect
        self._client.on_message    = self._on_message
        self._client.on_disconnect = self._on_disconnect

    def _make_client(self):
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
            self._log(f"MQTT connected to {self.host}:{self.port}", "info")
            self._log(f"MQTT SUBSCRIBE  {self.resp_topic}", "info")
            client.subscribe(self.resp_topic)
        else:
            self._log(f"MQTT connect refused: {reason_code}", "error")

    def _on_disconnect(self, client, userdata, flags=None, reason_code=None, properties=None):
        self._connected = False
        self._log(f"MQTT disconnected (reason={reason_code})", "warn")

    def _on_message(self, client, userdata, msg):
        raw = msg.payload.decode('utf-8')
        self._log(f"MQTT RX  [{msg.topic}]  {raw}", "info")
        try:
            self._queue.put(json.loads(raw))
        except Exception as exc:
            self._log(f"MQTT RX parse error: {exc}", "error")

    def connect(self) -> None:
        self._log(f"MQTT connecting to {self.host}:{self.port} ...", "info")
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

    def _drain(self):
        while not self._queue.empty():
            self._queue.get_nowait()

    def _publish(self, msg_name: str, data: dict) -> None:
        payload = json.dumps({"msg": msg_name, "data": data})
        self._log(f"MQTT TX  [{self.cmd_topic}]  {payload}", "info")
        self._client.publish(self.cmd_topic, payload)

    def _wait_response(self, key_hex: str, timeout: float = 5.0) -> dict:
        try:
            return self._queue.get(timeout=timeout)
        except queue.Empty:
            self._log(
                f"MQTT response timeout for key {key_hex} — no message on [{self.resp_topic}]",
                "error",
            )
            raise TransportError(f"MQTT response timeout for key {key_hex}")

    def get_value(self, key: int) -> Tuple[int, List[int]]:
        self._drain()
        self._publish("MsgConfigGetKey", {"key": key})
        resp = self._wait_response(f"0x{key:04X}")
        d = resp.get("data", {})
        return int(d.get("type", 0)), list(d.get("data", []))

    def set_value(self, key: int, type_id: int, data: List[int]) -> None:
        self._publish("MsgConfigSet", {
            "key": key, "type": type_id, "size": len(data), "data": data
        })
        time.sleep(0.1)

    def get_dev_info_value(self, key: int) -> Tuple[bool, str]:
        self._drain()
        self._publish("MsgDevInfoRead", {"key": key, "source_module_id": 0})
        resp = self._wait_response(f"0x{key:04X}")
        d = resp.get("data", {})
        return bool(d.get("is_valid", False)), str(d.get("value", ""))


# ─────────────────────────────────────────────────────────────────────────────
# UART Transport
# ─────────────────────────────────────────────────────────────────────────────

class UARTTransport:
    def __init__(self, port: str, baud: int):
        try:
            import serial
            self._serial_mod = serial
        except ImportError:
            raise TransportError("pyserial not installed — run: pip install pyserial")
        self.port  = port
        self.baud  = baud
        self._ser  = None
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


# ─────────────────────────────────────────────────────────────────────────────
# MQTT Raw Client (for MessageTree / command panel — FERP MQTT Tool mode)
# ─────────────────────────────────────────────────────────────────────────────

def _build_topic_base(dev_type: str, group: str, device_id: str) -> str:
    safe_id = device_id.replace(":", "").replace("-", "").lower()
    return f"ferp/{dev_type}/{group}/{safe_id}"

def _cmd_topic(base: str)  -> str: return f"{base}/cmd"
def _resp_topic(base: str) -> str: return f"{base}/resp"
def _evt_topic(base: str)  -> str: return f"{base}/evt"


class MqttRawClient:
    """
    Low-level MQTT client used by the FERP message tree panel.
    Sends arbitrary JSON commands and forwards all responses to the console.
    """

    def __init__(self, on_log, on_status_change):
        self._on_log    = on_log
        self._on_status = on_status_change
        self._client    = None
        self._base      = ""
        self._connected = False

    def connect(self, broker: str, port: int, base: str) -> None:
        if self._client:
            self._client.disconnect()
            self._client.loop_stop()
        self._base = base
        cid = f"ferp-gui-{random.randint(1000, 9999)}"
        try:
            import paho.mqtt.client as mqtt_mod
        except ImportError:
            self._on_log("[error] paho-mqtt not installed", "error")
            return

        try:
            from paho.mqtt.enums import CallbackAPIVersion
            self._client = mqtt_mod.Client(
                client_id=cid,
                callback_api_version=CallbackAPIVersion.VERSION2,
            )
        except ImportError:
            self._client = mqtt_mod.Client(client_id=cid)

        self._client.on_connect    = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message    = self._on_message
        try:
            self._client.connect(broker, port, keepalive=60)
            self._client.loop_start()
        except Exception as exc:
            self._on_log(f"[error] Could not connect: {exc}", "error")
            self._on_status(False)

    def disconnect(self) -> None:
        if self._client:
            self._client.disconnect()
            self._client.loop_stop()
            self._client = None
        self._connected = False
        self._on_status(False)

    def send(self, msg_name: str, data: dict) -> None:
        if not self._client or not self._connected:
            self._on_log("[error] Not connected", "error")
            return
        seq     = random.randint(1, 0xFFFF_FFFF)
        payload = {"seq": seq, "msg": msg_name, "data": data}
        self._on_log(f"→ [cmd]  {json.dumps(payload)}", "cmd")
        self._client.publish(_cmd_topic(self._base), json.dumps(payload), qos=1)

    @property
    def is_connected(self) -> bool:
        return self._connected

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        ok = (hasattr(reason_code, 'is_failure') and not reason_code.is_failure) or reason_code == 0
        if ok:
            self._connected = True
            self._on_status(True)
            self._on_log(f"[info]  Connected  base={self._base}", "info")
            client.subscribe(_resp_topic(self._base), qos=1)
            client.subscribe(_evt_topic(self._base), qos=0)
            self._on_log("[info]  Subscribed to resp + evt", "info")
        else:
            self._on_log(f"[error] Connection refused rc={reason_code}", "error")
            self._on_status(False)

    def _on_disconnect(self, client, userdata, flags=None, reason_code=None, properties=None):
        self._connected = False
        self._on_status(False)
        msg = f"[warn]  Unexpected disconnect rc={reason_code}" if reason_code else "[info]  Disconnected"
        self._on_log(msg, "warn" if reason_code else "info")

    def _on_message(self, client, userdata, message):
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception as exc:
            self._on_log(f"[warn]  Bad payload on {message.topic}: {exc}", "warn")
            return
        suffix = message.topic.split("/")[-1]
        self._on_log(f"← [{suffix}]  {json.dumps(payload, indent=2)}", suffix)
