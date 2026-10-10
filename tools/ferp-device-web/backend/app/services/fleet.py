"""
Fleet — presence of every device seen on the broker (registered or not).

Every resp/evt message updates the sender's last-seen time; MsgDevInfoValue
responses also fill in device info (FW version, ...). A probe reads the FW
version from devices to confirm they are alive, on demand or periodically
(fleet.auto_probe_interval_s; off by default so nothing is sent unasked).

Presence is persisted in the DocumentStore ("presence") so last-seen survives
restarts. Changes are pushed as batched `fleet.update` events.
"""

import logging
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from typing import Callable, Optional

from ferp_core import topics

from ..models import AppConfig, Device
from ..ports import DeviceRepository, DocumentStore, EventBus
from .catalog import Catalog
from .console import Console
from .device_ops import DeviceOps
from .mqtt_hub import MqttHub

log = logging.getLogger("ferp.fleet")

_COLLECTION = "presence"


class Fleet:
    def __init__(self, hub: MqttHub, repo: DeviceRepository, ops: DeviceOps, catalog: Catalog,
                 docs: DocumentStore, bus: EventBus, console: Console, get_config: Callable[[], AppConfig]):
        self._hub, self._repo, self._ops, self._catalog = hub, repo, ops, catalog
        self._docs, self._bus, self._console, self._get_config = docs, bus, console, get_config
        self._lock = threading.Lock()
        self._presence: dict[str, dict] = {p["topic_id"]: p for p in docs.list(_COLLECTION) if "topic_id" in p}
        self._dirty: set[str] = set()
        self._unsaved: set[str] = set()
        self._pool = ThreadPoolExecutor(max_workers=8, thread_name_prefix="probe")
        hub.add_listener(self._on_message)
        threading.Thread(target=self._loop, daemon=True, name="fleet").start()

    # ── presence updates ──────────────────────────────────────────────────────

    def _entry(self, topic_id: str, group: str) -> dict:
        e = self._presence.get(topic_id)
        if e is None:
            e = self._presence[topic_id] = {"topic_id": topic_id, "group": group, "first_seen": None,
                                            "last_seen": None, "last_msg": None, "last_kind": None,
                                            "msg_count": 0, "info": {}, "probe": None}
        if group:
            e["group"] = group
        return e

    def _touch(self, topic_id: str) -> None:
        self._dirty.add(topic_id)
        self._unsaved.add(topic_id)

    def _on_message(self, topic_id: str, kind: str, payload: dict, group: str = "") -> None:
        now = time.time()
        with self._lock:
            e = self._entry(topic_id, group)
            e["first_seen"] = e["first_seen"] or now
            e["last_seen"], e["last_msg"], e["last_kind"] = now, payload.get("msg"), kind
            e["msg_count"] += 1
            data = payload.get("data") or {}
            if payload.get("msg") == "MsgDevInfoValue" and data.get("is_valid"):
                field = next((f for k, _, f in self._catalog.devinfo_keys if k == data.get("key")), None)
                if field:
                    e["info"][field] = str(data.get("value", ""))
            self._touch(topic_id)

    # ── views ─────────────────────────────────────────────────────────────────

    def snapshot(self) -> dict:
        with self._lock:
            presence = {k: dict(v) for k, v in self._presence.items()}
        rows, seen = [], set()
        for d in self._repo.list():
            tid = topics.topic_id(d.mqtt_id) if d.mqtt_id else ""
            seen.add(tid)
            rows.append(self._row(presence.get(tid), d, tid))
        for tid, p in presence.items():
            if tid not in seen:
                rows.append(self._row(p, None, tid))
        return {"online_timeout_s": self._get_config().fleet.online_timeout_s, "now": time.time(), "devices": rows}

    @staticmethod
    def _row(p: Optional[dict], d: Optional[Device], tid: str) -> dict:
        p = p or {}
        site = {k: getattr(d, k) for k in ("device_type", "shed", "pump_id_1", "pump_id_2", "pump_type",
                                           "board_version", "sd_card_size")} if d else {}
        return {"device_id": d.id if d else None, "label": d.label if d else None,
                "registered": d is not None, "topic_id": tid, **site,
                "group": (d.group if d else None) or p.get("group") or "default",
                "first_seen": p.get("first_seen"), "last_seen": p.get("last_seen"),
                "last_msg": p.get("last_msg"), "last_kind": p.get("last_kind"),
                "msg_count": p.get("msg_count", 0), "info": p.get("info", {}), "probe": p.get("probe")}

    def info(self, topic_id: str) -> dict:
        """Device-info fields last seen from the device (e.g. hw_version), {} if none."""
        with self._lock:
            return dict((self._presence.get(topic_id) or {}).get("info") or {})

    def forget(self, topic_id: str) -> bool:
        """Drop presence for an unregistered device (it reappears if it sends again)."""
        with self._lock:
            found = self._presence.pop(topic_id, None) is not None
            self._dirty.discard(topic_id)
            self._unsaved.discard(topic_id)
        if found:
            self._docs.delete(_COLLECTION, topic_id)
            self._bus.publish({"type": "fleet.removed", "topic_id": topic_id})
        return found

    # ── probing ───────────────────────────────────────────────────────────────

    def probe(self, devices: list[Device], source: str = "manual") -> int:
        """Read FW version from each device in parallel; results arrive as fleet.update."""
        fw_key = next((k for k, _, f in self._catalog.devinfo_keys if f == "fw_version"), 0xA004)
        targets = [d for d in devices if d.mqtt_id]
        if not targets:
            return 0
        self._console.log("info", f"Probing {len(targets)} device(s) ({source})")
        for d in targets:
            self._pool.submit(self._probe_one, d, fw_key)
        return len(targets)

    def _probe_one(self, dev: Device, fw_key: int) -> None:
        tid = topics.topic_id(dev.mqtt_id)
        t0 = time.time()
        try:
            self._ops.read_devinfo(dev, fw_key, timeout=self._get_config().fleet.probe_timeout_s)
            result = {"ts": time.time(), "ok": True, "rtt_ms": int((time.time() - t0) * 1000)}
        except Exception as exc:
            result = {"ts": time.time(), "ok": False, "error": str(exc)}
        with self._lock:
            self._entry(tid, dev.group)["probe"] = result
            self._touch(tid)

    # ── background: push changes, persist, auto-probe ─────────────────────────

    def _loop(self) -> None:
        last_save = last_auto = time.time()
        while True:
            time.sleep(2)
            try:
                with self._lock:
                    dirty, self._dirty = self._dirty, set()
                if dirty:
                    rows = [r for r in self.snapshot()["devices"] if r["topic_id"] in dirty]
                    self._bus.publish({"type": "fleet.update", "devices": rows})
                now = time.time()
                if now - last_save >= 30:
                    last_save = now
                    self._save()
                interval = self._get_config().fleet.auto_probe_interval_s
                if interval and now - last_auto >= interval and self._hub.connected:
                    last_auto = now
                    self.probe(self._repo.list(), source="auto")
            except Exception:
                log.exception("fleet loop error")

    def _save(self) -> None:
        with self._lock:
            todo, self._unsaved = self._unsaved, set()
            docs = [dict(self._presence[t]) for t in todo if t in self._presence]
        for d in docs:
            self._docs.put(_COLLECTION, d["topic_id"], d)

    def close(self) -> None:
        self._save()
