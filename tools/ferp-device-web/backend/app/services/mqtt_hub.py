"""
MqttHub — the backend's single, long-lived MQTT connection.

* Connects at start-up when mqtt.auto_connect is set and keeps reconnecting
  (paho loop thread) until disconnect() is called from the UI.
* Subscribes to resp + evt of *all* devices of the configured dev_type, so the
  console sees everything and later features (fleet view) need no new plumbing.
* publish_cmd()  — fire-and-forget signed command (message tree "Send").
* request()      — signed command + wait for the matching response. The firmware
                   echoes the command's seq in its response envelope, so a
                   response is matched on (device, seq, msg name, key).
"""

import itertools
import json
import logging
import random
import threading
import time
from typing import Callable, Optional

import paho.mqtt.client as mqtt
from paho.mqtt.enums import CallbackAPIVersion

from ferp_core import topics
from ferp_core.mqtt_auth import sign

from ..models import MqttConfig
from ..ports import EventBus
from .console import Console

log = logging.getLogger("ferp.mqtt")


class NotConnectedError(RuntimeError):
    pass


class ResponseTimeout(TimeoutError):
    pass


class _Waiter:
    __slots__ = ("topic_id", "seq", "msg", "key", "event", "result")

    def __init__(self, topic_id: str, seq: int, msg: str, key: Optional[int]):
        self.topic_id, self.seq, self.msg, self.key = topic_id, seq, msg, key
        self.event  = threading.Event()
        self.result: Optional[dict] = None

    def matches(self, topic_id: str, payload: dict) -> bool:
        if topic_id != self.topic_id or payload.get("msg") != self.msg:
            return False
        if "seq" in payload and payload["seq"] != self.seq:
            return False
        if self.key is not None and (payload.get("data") or {}).get("key") != self.key:
            return False
        return True


Listener = Callable[[str, str, dict, str], None]   # (topic_id, kind, payload, group)


class MqttHub:
    def __init__(self, get_config: Callable[[], MqttConfig], console: Console, bus: EventBus):
        self._get_config = get_config
        self._console    = console
        self._bus        = bus
        self._client: Optional[mqtt.Client] = None
        self._cfg: Optional[MqttConfig] = None
        self._lock       = threading.RLock()
        self._want       = False
        self._state      = "disconnected"   # disconnected | connecting | connected | reconnecting
        self._error: Optional[str] = None
        self._since      = time.time()
        self._seq        = itertools.count(random.randint(1, 0xFFFF))
        self._waiters: list[_Waiter] = []
        self._waiters_lock = threading.Lock()
        self._device_locks: dict[str, threading.RLock] = {}
        self._device_locks_lock = threading.Lock()
        self._listeners: list[Listener] = []

    # ── status ────────────────────────────────────────────────────────────────

    def status(self) -> dict:
        cfg = self._cfg or self._get_config()
        return {"state": self._state, "connected": self._state == "connected",
                "host": cfg.host, "port": cfg.port, "dev_type": cfg.dev_type,
                "error": self._error, "since": self._since}

    def _set_state(self, state: str, error: Optional[str] = None) -> None:
        self._state, self._error, self._since = state, error, time.time()
        self._bus.publish({"type": "mqtt.status", "status": self.status()})

    @property
    def connected(self) -> bool:
        return self._state == "connected"

    @property
    def dev_type(self) -> str:
        return (self._cfg or self._get_config()).dev_type

    def add_listener(self, fn: Listener) -> None:
        self._listeners.append(fn)

    # ── connect / disconnect ──────────────────────────────────────────────────

    def connect(self) -> None:
        with self._lock:
            self._teardown()
            cfg = self._cfg = self._get_config()
            cid = cfg.client_id or f"ferp-web-{random.randint(10000, 99999)}"
            c = mqtt.Client(callback_api_version=CallbackAPIVersion.VERSION2, client_id=cid)
            if cfg.username:
                c.username_pw_set(cfg.username, cfg.password or None)
            c.on_connect      = self._on_connect
            c.on_connect_fail = self._on_connect_fail
            c.on_disconnect   = self._on_disconnect
            c.on_message      = self._on_message
            c.reconnect_delay_set(min_delay=1, max_delay=30)
            self._client, self._want = c, True
            self._set_state("connecting")
            self._console.log("info", f"MQTT connecting to {cfg.host}:{cfg.port} as {cid} ...")
            try:
                c.connect_async(cfg.host, cfg.port, keepalive=cfg.keepalive)
                c.loop_start()
            except Exception as exc:          # e.g. bad host name
                self._console.log("error", f"MQTT connect failed: {exc}")
                self._set_state("reconnecting", str(exc))
                c.loop_start()                # loop keeps retrying

    def disconnect(self) -> None:
        with self._lock:
            was = self._client is not None
            self._teardown()
            self._set_state("disconnected")
            if was:
                self._console.log("info", "MQTT disconnected (by user)")

    def _teardown(self) -> None:
        self._want = False
        c, self._client = self._client, None
        if c is not None:
            try:
                c.disconnect()
            except Exception:
                pass
            c.loop_stop()

    # ── paho callbacks (paho network thread) ──────────────────────────────────

    def _on_connect(self, client, userdata, flags, reason_code, properties=None):
        if client is not self._client:
            return
        if reason_code.is_failure:
            self._console.log("error", f"MQTT connect refused: {reason_code}")
            self._set_state("reconnecting", str(reason_code))
            return
        dev_type = self._cfg.dev_type
        client.subscribe([(topics.wildcard(dev_type, "resp"), 1), (topics.wildcard(dev_type, "evt"), 0)])
        self._set_state("connected")
        self._console.log("info", f"MQTT connected to {self._cfg.host}:{self._cfg.port} — "
                                  f"listening on {topics.wildcard(dev_type, 'resp|evt')}")

    def _on_connect_fail(self, client, userdata):
        if client is not self._client:
            return
        if self._state != "reconnecting":
            self._console.log("warn", f"MQTT broker {self._cfg.host}:{self._cfg.port} unreachable — retrying")
        self._set_state("reconnecting", "broker unreachable")

    def _on_disconnect(self, client, userdata, flags, reason_code, properties=None):
        if client is not self._client or not self._want:
            return
        self._console.log("warn", f"MQTT connection lost ({reason_code}) — reconnecting")
        self._set_state("reconnecting", str(reason_code))

    def _on_message(self, client, userdata, message):
        parsed = topics.parse(message.topic)
        if not parsed:
            return
        _dev_type, group, topic_id, kind = parsed
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception as exc:
            self._console.log("warn", f"Bad payload on {message.topic}: {exc}", device=topic_id, topic=message.topic)
            return
        if not isinstance(payload, dict):
            return
        level = "resp" if kind == "resp" else "evt"
        self._console.log(level, f"← [{kind}] {json.dumps(payload)}", device=topic_id, topic=message.topic)

        if kind == "resp":
            with self._waiters_lock:
                for w in self._waiters:
                    if w.result is None and w.matches(topic_id, payload):
                        w.result = payload
                        w.event.set()
                        break
        for fn in self._listeners:
            try:
                fn(topic_id, kind, payload, group)
            except Exception:
                log.exception("listener failed")

    # ── sending ───────────────────────────────────────────────────────────────

    def _next_seq(self) -> int:
        seq = next(self._seq) & 0xFFFF_FFFF
        return seq or next(self._seq) & 0xFFFF_FFFF    # firmware treats seq 0 as "event"

    def publish_cmd(self, base: str, msg: str, data: dict) -> int:
        """Send a signed command to {base}/cmd. Returns the seq used."""
        client = self._client
        if client is None or not self.connected:
            raise NotConnectedError("MQTT is not connected")
        with self.device_lock(base):      # never interleave with another device's pending request
            seq = self._next_seq()
            hop_idx, hash_hex = sign(seq)
            payload = json.dumps({"seq": seq, "hop_idx": hop_idx, "hash": hash_hex, "msg": msg, "data": data})
            topic = f"{base}/cmd"
            client.publish(topic, payload, qos=1)
        self._console.log("cmd", f"→ [cmd] {payload}", device=base.rsplit("/", 1)[-1], topic=topic)
        return seq

    def request(self, base: str, msg: str, data: dict, expect_msg: str,
                key: Optional[int] = None, timeout: float = 5.0) -> dict:
        """Send a command and block until the matching response arrives."""
        topic_id = base.rsplit("/", 1)[-1]
        with self.device_lock(base):
            # The seq is only known once published, so register the waiter under
            # the lock with a placeholder and fill the seq in before any reply can match.
            waiter = _Waiter(topic_id, -1, expect_msg, key)
            with self._waiters_lock:
                self._waiters.append(waiter)
                try:
                    waiter.seq = self.publish_cmd(base, msg, data)
                except Exception:
                    self._waiters.remove(waiter)
                    raise
            try:
                if not waiter.event.wait(timeout):
                    raise ResponseTimeout(f"No {expect_msg} from {topic_id} within {timeout:g}s")
                return waiter.result
            finally:
                with self._waiters_lock:
                    self._waiters.remove(waiter)

    def device_lock(self, base: str) -> threading.RLock:
        """One outstanding request per device: the firmware echoes only the last cmd seq.
        Re-entrant, so callers can hold it across several commands (write + read-back).
        Lock order: device lock → _waiters_lock; _device_locks_lock is a leaf."""
        with self._device_locks_lock:
            return self._device_locks.setdefault(base, threading.RLock())
