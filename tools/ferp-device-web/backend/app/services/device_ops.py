"""
DeviceOps — config key read/write and device-info reads over MqttHub.

Batch operations run as background jobs (they survive the browser closing) and
report through the event bus:

    config.value    {device_id, key, type_id, value_type, value, ts}
    config.written  {device_id, key, value, verified}
    devinfo.value   {device_id, key, valid, value, ts}
    job.progress    {job_id, device_id, kind, done, total}
    job.done        {job_id, device_id, kind, ok, cancelled, errors}

Values are cached per device so every open browser sees the latest readings.
Any MsgConfigValue / MsgDevInfoValue response updates the cache, including
ones triggered by commands sent from the message tree.
"""

import logging
import threading
import time
import uuid
from typing import Callable, Optional

from ferp_core import topics
from ferp_core.transports import TYPE_NAMES, decode_value, encode_value

from ..models import AppConfig, Device
from ..ports import DeviceRepository, EventBus
from .audit import Audit, is_secret
from .catalog import Catalog
from .console import Console
from .mqtt_hub import MqttHub, NotConnectedError, ResponseTimeout

log = logging.getLogger("ferp.device")


class DeviceOps:
    def __init__(self, hub: MqttHub, repo: DeviceRepository, catalog: Catalog,
                 console: Console, bus: EventBus, get_config: Callable[[], AppConfig],
                 audit: Optional[Audit] = None):
        self._hub, self._repo, self._catalog, self._audit = hub, repo, catalog, audit
        self._console, self._bus, self._get_config = console, bus, get_config
        self._cache: dict[str, dict] = {}          # topic_id → {"config": {}, "devinfo": {}}
        self._cache_lock = threading.Lock()
        self._jobs: dict[str, dict] = {}
        hub.add_listener(self._on_message)

    # ── addressing ────────────────────────────────────────────────────────────

    def base(self, dev: Device) -> str:
        if not dev.mqtt_id:
            raise ValueError(f"Device '{dev.label}' has neither UUID nor MAC")
        return topics.device_base(self._hub.dev_type, dev.group or "default", dev.mqtt_id)

    def _device_ids_for(self, topic_id: str) -> list[str]:
        return [d.id for d in self._repo.list() if d.mqtt_id and topics.topic_id(d.mqtt_id) == topic_id]

    # ── cache / incoming responses ────────────────────────────────────────────

    def cached_config(self, dev: Device) -> dict[str, dict]:
        """Last value read from the device per key (str(key_id) → entry)."""
        tid = topics.topic_id(dev.mqtt_id) if dev.mqtt_id else ""
        with self._cache_lock:
            return dict(self._cache.get(tid, {}).get("config", {}))

    def state(self, dev: Device) -> dict:
        tid = topics.topic_id(dev.mqtt_id) if dev.mqtt_id else ""
        with self._cache_lock:
            c = self._cache.get(tid, {"config": {}, "devinfo": {}})
            return {"config": dict(c["config"]), "devinfo": dict(c["devinfo"]),
                    "jobs": [{k: v for k, v in j.items() if k != "cancel"}
                             for j in self._jobs.values() if j["device_id"] == dev.id and j["running"]]}

    def _on_message(self, topic_id: str, kind: str, payload: dict, group: str = "") -> None:
        if kind != "resp":
            return
        msg, data = payload.get("msg"), payload.get("data") or {}
        now = time.time()
        if msg == "MsgConfigValue" and "key" in data:
            type_id = int(data.get("type", 0))
            entry = {"key": int(data["key"]), "type_id": type_id, "value_type": TYPE_NAMES.get(type_id, str(type_id)),
                     "value": decode_value(list(data.get("data") or []), type_id), "ts": now}
            self._store(topic_id, "config", entry, "config.value")
        elif msg == "MsgDevInfoValue" and "key" in data:
            entry = {"key": int(data["key"]), "valid": bool(data.get("is_valid", False)),
                     "value": str(data.get("value", "")), "ts": now}
            self._store(topic_id, "devinfo", entry, "devinfo.value")

    def _store(self, topic_id: str, section: str, entry: dict, event_type: str) -> None:
        with self._cache_lock:
            self._cache.setdefault(topic_id, {"config": {}, "devinfo": {}})[section][str(entry["key"])] = entry
        for device_id in self._device_ids_for(topic_id):
            self._bus.publish({**entry, "type": event_type, "device_id": device_id})

    # ── single operations (blocking — call from a worker thread) ──────────────

    def send(self, dev: Device, msg: str, data: dict) -> int:
        return self._hub.publish_cmd(self.base(dev), msg, data)

    def read_config(self, dev: Device, key: int) -> dict:
        resp = self._hub.request(self.base(dev), "MsgConfigGetKey", {"key": key},
                                 expect_msg="MsgConfigValue", key=key, timeout=self._timeout())
        d = resp.get("data") or {}
        type_id = int(d.get("type", 0))
        return {"key": key, "type_id": type_id, "value": decode_value(list(d.get("data") or []), type_id)}

    def write_config(self, dev: Device, key: int, value: str, user: str = "system") -> bool:
        """Write a key. Returns True when verified by read-back (or verification is off)."""
        ck = self._catalog.config_key(key)
        if ck is None:
            raise ValueError(f"Unknown config key 0x{key:04X}")
        data = encode_value(value, ck.type_id)
        base = self.base(dev)
        verified, got = True, None
        with self._hub.device_lock(base):
            self._hub.publish_cmd(base, "MsgConfigSet",
                                  {"key": key, "type": ck.type_id, "size": len(data), "data": data})
            expected = decode_value(data, ck.type_id)
            if self._get_config().device.verify_writes:
                time.sleep(0.15)
                got = self.read_config(dev, key)["value"]
                verified = got == expected
        if not verified:
            self._console.log("warn", f"WRITE 0x{key:04X} {ck.name}: read back {got!r}, expected {expected!r}",
                              device=topics.topic_id(dev.mqtt_id))
        for device_id in self._device_ids_for(topics.topic_id(dev.mqtt_id)):
            self._bus.publish({"type": "config.written", "device_id": device_id, "key": key,
                               "value": value, "verified": verified})
        if self._audit:
            self._audit.record(user, "config.write", dev, key=f"0x{key:04X}", name=ck.name,
                               value="***" if is_secret(ck.name) else value, verified=verified)
        return verified

    def read_site_info(self, dev: Device) -> dict:
        """Read the registry fields the device itself knows: pump IDs and board version."""
        out, errors = {}, []
        for field, fn in (("pump_id_1", lambda: self.read_config(dev, 0x6007)["value"]),
                          ("pump_id_2", lambda: self.read_config(dev, 0x6008)["value"]),
                          ("board_version", lambda: self._devinfo_value(dev, "hw_version"))):
            try:
                out[field] = fn()
            except (ResponseTimeout, NotConnectedError) as exc:
                errors.append(f"{field}: {exc}")
                break                                      # device unreachable — stop
            except Exception as exc:
                errors.append(f"{field}: {exc}")
        return {"values": out, "errors": errors}

    def _devinfo_value(self, dev: Device, field: str) -> str:
        key = next((k for k, _, f in self._catalog.devinfo_keys if f == field), None)
        if key is None:
            raise ValueError(f"No device-info key for {field}")
        r = self.read_devinfo(dev, key)
        return r["value"] if r["valid"] else ""

    def read_devinfo(self, dev: Device, key: int, timeout: Optional[float] = None) -> dict:
        resp = self._hub.request(self.base(dev), "MsgDevInfoRead", {"key": key, "source_module_id": 0},
                                 expect_msg="MsgDevInfoValue", key=key, timeout=timeout or self._timeout())
        d = resp.get("data") or {}
        return {"key": key, "valid": bool(d.get("is_valid", False)), "value": str(d.get("value", ""))}

    def _timeout(self) -> float:
        return self._get_config().device.response_timeout_s

    # ── jobs ──────────────────────────────────────────────────────────────────

    def start_read_config(self, dev: Device, keys: Optional[list[int]]) -> str:
        keys = keys or [k.key_id for k in self._catalog.config_keys]
        return self._start_job(dev, "config.read", keys, lambda k: self.read_config(dev, k))

    def start_write_config(self, dev: Device, values: list[tuple[int, str]], user: str = "system") -> str:
        return self._start_job(dev, "config.write", values, lambda kv: self.write_config(dev, *kv, user=user))

    def start_read_devinfo(self, dev: Device, keys: Optional[list[int]]) -> str:
        keys = keys or [k for k, _, _ in self._catalog.devinfo_keys]
        return self._start_job(dev, "devinfo.read", keys, lambda k: self.read_devinfo(dev, k))

    def cancel_job(self, job_id: str) -> bool:
        job = self._jobs.get(job_id)
        if not job or not job["running"]:
            return False
        job["cancel"].set()
        return True

    def _start_job(self, dev: Device, kind: str, items: list, fn) -> str:
        if not self._hub.connected:
            raise NotConnectedError("MQTT is not connected")
        self.base(dev)   # validate addressing before starting
        job_id = uuid.uuid4().hex[:10]
        job = {"job_id": job_id, "device_id": dev.id, "kind": kind, "total": len(items),
               "done": 0, "running": True, "cancel": threading.Event()}
        self._jobs[job_id] = job
        threading.Thread(target=self._run_job, args=(job, dev, items, fn), daemon=True,
                         name=f"job-{kind}-{job_id}").start()
        return job_id

    def _run_job(self, job: dict, dev: Device, items: list, fn) -> None:
        tid = topics.topic_id(dev.mqtt_id)
        errors, cancelled = [], False
        pub = lambda t, **kw: self._bus.publish({"type": t, "job_id": job["job_id"], "device_id": dev.id,
                                                 "kind": job["kind"], **kw})
        pub("job.progress", done=0, total=job["total"])
        for item in items:
            if job["cancel"].is_set():
                cancelled = True
                self._console.log("warn", f"{job['kind']} cancelled", device=tid)
                break
            label = f"0x{(item[0] if isinstance(item, tuple) else item):04X}"
            try:
                fn(item)
            except (ResponseTimeout, NotConnectedError) as exc:
                errors.append(f"{label}: {exc}")
                self._console.log("error", f"{job['kind']} {label}: {exc} — aborting batch (device unreachable)", device=tid)
                break
            except Exception as exc:
                errors.append(f"{label}: {exc}")
                self._console.log("error", f"{job['kind']} {label}: {exc}", device=tid)
            job["done"] += 1
            pub("job.progress", done=job["done"], total=job["total"])
        job["running"] = False
        pub("job.done", ok=not errors and not cancelled, cancelled=cancelled, errors=errors)
        # keep the job table small
        for jid in [j for j, v in self._jobs.items() if not v["running"]][:-50]:
            self._jobs.pop(jid, None)
