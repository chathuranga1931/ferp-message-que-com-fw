"""
Firmware library (.bdl bundles in the BlobStore) and OTA sessions over MQTT.

OTA reuses ferp_core.ota_session.OtaSession unchanged — it opens its own MQTT
connection to the configured broker. Events:

    ota.state     {device_id, session}
    ota.progress  {device_id, pct}
    ota.log       {device_id, text, level}
    ota.batch     {batch}
"""

import hashlib
import json
import logging
import os
import tempfile
import threading
import time
import uuid
from collections import deque
from pathlib import Path
from typing import Callable, Optional

from ferp_core import topics
from ferp_core.ota_bundle import decode_bundle
from ferp_core.ota_session import OtaSession

from ..models import AppConfig, Device
from ..ports import BlobStore, EventBus
from .audit import Audit
from .console import Console

log = logging.getLogger("ferp.ota")

_PREFIX = "firmware/"


def _bundle_id(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()[:12]


class FirmwareLibrary:
    def __init__(self, blobs: BlobStore, bundle_dirs: Callable[[], list[Path]] = lambda: []):
        self._blobs = blobs
        self._bundle_dirs = bundle_dirs

    def add(self, filename: str, data: bytes, user: str = "system", source: str = "upload",
            notes: str = "") -> dict:
        """Store a bundle. Re-adding identical bytes returns the existing entry with duplicate=True."""
        hdr, fw = decode_bundle(data)          # raises ValueError for a bad bundle
        fid = _bundle_id(data)
        existing = self.meta(fid)
        if existing:
            return {**existing, "duplicate": True}
        meta = {"id": fid, "filename": filename, "name": hdr.name, "version": hdr.version,
                "built": hdr.timestamp, "size": len(fw), "uploaded": time.time(),
                "uploaded_by": user, "source": source, "notes": notes}
        self._blobs.put(f"{_PREFIX}{fid}.bdl", data)
        self._blobs.put(f"{_PREFIX}{fid}.json", json.dumps(meta).encode())
        return meta

    def meta(self, fid: str) -> Optional[dict]:
        try:
            return json.loads(self._blobs.get(f"{_PREFIX}{fid}.json"))
        except KeyError:
            return None

    def set_notes(self, fid: str, notes: str) -> dict:
        meta = self.meta(fid)
        if meta is None:
            raise KeyError(f"Unknown bundle {fid}")
        meta["notes"] = notes
        self._blobs.put(f"{_PREFIX}{fid}.json", json.dumps(meta).encode())
        return meta

    def raw(self, fid: str) -> tuple[dict, bytes]:
        """Full .bdl bytes (for download)."""
        meta = self.meta(fid)
        if meta is None:
            raise KeyError(f"Unknown bundle {fid}")
        return meta, self._blobs.get(f"{_PREFIX}{fid}.bdl")

    # ── folders on this PC (e.g. the repo's releases/) ───────────────────────

    def scan(self) -> dict:
        """List .bdl files in the bundle folders with their header and library status."""
        dirs = [d for d in self._bundle_dirs() if d.is_dir()]
        in_lib = {m["id"] for m in self.list()}
        files = []
        for d in dirs:
            for p in sorted(d.rglob("*.bdl")):
                entry = {"path": str(p), "folder": str(d), "relpath": str(p.relative_to(d)),
                         "filename": p.name, "size": p.stat().st_size, "modified": p.stat().st_mtime}
                try:
                    data = p.read_bytes()
                    hdr, _ = decode_bundle(data)
                    entry.update(name=hdr.name, version=hdr.version, built=hdr.timestamp,
                                 id=_bundle_id(data), in_library=_bundle_id(data) in in_lib)
                except Exception as exc:
                    entry.update(error=str(exc), in_library=False)
                files.append(entry)
        files.sort(key=lambda f: f.get("built") or 0, reverse=True)
        return {"folders": [str(d) for d in self._bundle_dirs()], "files": files}

    def import_paths(self, paths: list[str], user: str) -> list[dict]:
        roots = [d.resolve() for d in self._bundle_dirs() if d.is_dir()]
        out = []
        for raw in paths:
            p = Path(raw).resolve()
            if p.suffix.lower() != ".bdl" or not any(r == p or r in p.parents for r in roots):
                out.append({"path": raw, "error": "not a .bdl inside a bundle folder"})
                continue
            try:
                out.append({"path": raw, **self.add(p.name, p.read_bytes(), user=user, source=f"folder: {p.parent}")})
            except (OSError, ValueError) as exc:
                out.append({"path": raw, "error": str(exc)})
        return out

    def list(self) -> list[dict]:
        items = []
        for key in self._blobs.list(_PREFIX):
            if key.endswith(".json"):
                try:
                    items.append(json.loads(self._blobs.get(key)))
                except Exception:
                    pass
        return sorted(items, key=lambda m: m.get("uploaded", 0), reverse=True)

    def get(self, fid: str) -> tuple[dict, bytes]:
        meta = json.loads(self._blobs.get(f"{_PREFIX}{fid}.json"))
        _, fw = decode_bundle(self._blobs.get(f"{_PREFIX}{fid}.bdl"))
        return meta, fw

    def delete(self, fid: str) -> None:
        self._blobs.delete(f"{_PREFIX}{fid}.bdl")
        self._blobs.delete(f"{_PREFIX}{fid}.json")


class OtaManager:
    def __init__(self, library: FirmwareLibrary, console: Console, bus: EventBus,
                 get_config: Callable[[], AppConfig], audit: Optional[Audit] = None):
        self._lib, self._console, self._bus, self._get_config = library, console, bus, get_config
        self._audit = audit
        self._sessions: dict[str, dict] = {}       # device_id → session info
        self._handles: dict[str, OtaSession] = {}
        self._done: dict[str, threading.Event] = {}
        self._lock = threading.Lock()

    def sessions(self) -> list[dict]:
        with self._lock:
            return [self._view(s) for s in self._sessions.values()]

    def session(self, device_id: str) -> Optional[dict]:
        with self._lock:
            s = self._sessions.get(device_id)
            return self._view(s) if s else None

    @staticmethod
    def _view(s: dict) -> dict:
        return {k: (list(v) if k == "log" else v) for k, v in s.items()}

    def wait(self, device_id: str, timeout: Optional[float] = None) -> bool:
        """Block until the device's current OTA finishes. Returns False on timeout."""
        ev = self._done.get(device_id)
        return ev.wait(timeout) if ev else True

    def start(self, dev: Device, firmware_id: str, chunk_size: Optional[int], user: str = "system") -> dict:
        if not dev.mqtt_id:
            raise ValueError(f"Device '{dev.label}' has neither UUID nor MAC")
        with self._lock:
            cur = self._sessions.get(dev.id)
            if cur and cur["state"] == "running":
                raise RuntimeError("An OTA is already running for this device")
        meta, fw = self._lib.get(firmware_id)
        cfg = self._get_config()
        chunk = chunk_size or cfg.ota.chunk_size

        fd, tmp_path = tempfile.mkstemp(suffix=".bin", prefix="ferp_ota_")
        with os.fdopen(fd, "wb") as fh:
            fh.write(fw)

        info = {"device_id": dev.id, "device_label": dev.label, "firmware_id": firmware_id,
                "target": meta["name"], "version": meta["version"], "chunk_size": chunk,
                "state": "running", "progress": 0, "started": time.time(), "finished": None,
                "log": deque(maxlen=400)}
        tid = topics.topic_id(dev.mqtt_id)
        done = threading.Event()

        def on_log(msg: str):
            level = "error" if "[error]" in msg else ("warn" if "[warn]" in msg else "info")
            for line in msg.splitlines():
                info["log"].append({"ts": time.time(), "level": level, "text": line})
                self._bus.publish({"type": "ota.log", "device_id": dev.id, "level": level, "text": line})
            self._console.log("ota", msg, device=tid)

        def on_progress(pct: int):
            info["progress"] = pct
            self._bus.publish({"type": "ota.progress", "device_id": dev.id, "pct": pct})

        def on_done(ok: bool):
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
            if info["state"] == "running":
                info["state"] = "succeeded" if ok else "failed"
            info["finished"] = time.time()
            with self._lock:
                self._handles.pop(dev.id, None)
            self._console.log("ota" if ok else "error",
                              f"OTA {info['state']} — {meta['name']} v{meta['version']}", device=tid)
            if self._audit:
                self._audit.record(user, "ota.result", dev, state=info["state"], target=meta["name"],
                                   version=meta["version"])
            self._publish_state(dev.id)
            done.set()

        session = OtaSession(
            broker=cfg.mqtt.host, port=cfg.mqtt.port,
            username=cfg.mqtt.username or None, password=cfg.mqtt.password or None,
            dev_type=cfg.mqtt.dev_type, group=dev.group or "default", device_id=dev.mqtt_id,
            firmware_path=tmp_path, target=meta["name"], version=meta["version"], chunk_size=chunk,
            on_log=on_log, on_progress=on_progress, on_done=on_done,
        )
        with self._lock:
            self._sessions[dev.id] = info
            self._handles[dev.id] = session
            self._done[dev.id] = done
        self._console.log("ota", f"Starting OTA — target={meta['name']} version={meta['version']} chunk={chunk}",
                          device=tid)
        if self._audit:
            self._audit.record(user, "ota.start", dev, target=meta["name"], version=meta["version"],
                               firmware=meta["filename"], chunk_size=chunk)
        self._publish_state(dev.id)
        session.start()
        return self._view(info)

    def abort(self, device_id: str, user: str = "system", device: Optional[Device] = None) -> bool:
        with self._lock:
            handle = self._handles.get(device_id)
            info = self._sessions.get(device_id)
        if not handle or not info:
            return False
        info["state"] = "aborted"
        threading.Thread(target=handle.abort, daemon=True).start()   # abort() waits up to 2 s
        if self._audit:
            self._audit.record(user, "ota.abort", device)
        self._publish_state(device_id)
        return True

    def abort_all(self) -> None:
        for device_id in list(self._handles):
            self.abort(device_id)

    def _publish_state(self, device_id: str) -> None:
        s = self.session(device_id)
        if s:
            s.pop("log", None)
            self._bus.publish({"type": "ota.state", "device_id": device_id, "session": s})


class BatchOta:
    """Flash one bundle to many devices, `concurrency` at a time."""

    def __init__(self, ota: OtaManager, library: FirmwareLibrary, console: Console, bus: EventBus,
                 audit: Optional[Audit] = None):
        self._ota, self._lib, self._console, self._bus, self._audit = ota, library, console, bus, audit
        self._batches: dict[str, dict] = {}
        self._lock = threading.Lock()

    def list(self) -> list[dict]:
        with self._lock:
            return sorted((self._copy(b) for b in self._batches.values()), key=lambda b: b["started"], reverse=True)

    @staticmethod
    def _copy(b: dict) -> dict:
        return {k: ([dict(i) for i in v] if k == "items" else v) for k, v in b.items() if not k.startswith("_")}

    def start(self, user: str, devices: list[Device], firmware_id: str, chunk_size: Optional[int],
              concurrency: int, stop_on_failure: bool) -> dict:
        meta, _ = self._lib.get(firmware_id)
        busy = [d.label for d in devices if (self._ota.session(d.id) or {}).get("state") == "running"]
        if busy:
            raise RuntimeError("OTA already running on: " + ", ".join(busy))
        batch = {"batch_id": uuid.uuid4().hex[:10], "firmware_id": firmware_id, "target": meta["name"],
                 "version": meta["version"], "concurrency": concurrency, "stop_on_failure": stop_on_failure,
                 "chunk_size": chunk_size, "state": "running", "started": time.time(), "finished": None,
                 "started_by": user, "_cancel": threading.Event(),
                 "items": [{"device_id": d.id, "label": d.label, "state": "pending"} for d in devices]}
        with self._lock:
            self._batches[batch["batch_id"]] = batch
        if self._audit:
            self._audit.record(user, "ota.batch.start", target=meta["name"], version=meta["version"],
                               devices=[d.label for d in devices], concurrency=concurrency)
        self._console.log("ota", f"Batch OTA {meta['name']} v{meta['version']} -> {len(devices)} device(s), "
                                 f"{concurrency} at a time")
        threading.Thread(target=self._run, args=(batch, {d.id: d for d in devices}, user), daemon=True,
                         name=f"ota-batch-{batch['batch_id']}").start()
        self._publish(batch)
        return self._copy(batch)

    def cancel(self, user: str, batch_id: str) -> bool:
        b = self._batches.get(batch_id)
        if not b or b["state"] != "running":
            return False
        b["_cancel"].set()
        for item in b["items"]:
            if item["state"] == "running":
                self._ota.abort(item["device_id"], user)
        if self._audit:
            self._audit.record(user, "ota.batch.cancel", target=b["target"], version=b["version"])
        return True

    def _run(self, batch: dict, devices: dict[str, Device], user: str) -> None:
        pending = list(batch["items"])
        running: list[dict] = []
        failed = False
        while pending or running:
            stop = batch["_cancel"].is_set() or (failed and batch["stop_on_failure"])
            while not stop and pending and len(running) < batch["concurrency"]:
                item = pending.pop(0)
                try:
                    self._ota.start(devices[item["device_id"]], batch["firmware_id"], batch["chunk_size"], user=user)
                    item["state"] = "running"
                    running.append(item)
                except Exception as exc:
                    item["state"], item["error"] = "failed", str(exc)
                    failed = True
                    stop = batch["stop_on_failure"]
                self._publish(batch)
            if stop and pending:
                for item in pending:
                    item["state"] = "skipped"
                pending = []
                self._publish(batch)
            if not running:
                continue
            for item in list(running):
                if self._ota.wait(item["device_id"], timeout=0.5):
                    s = self._ota.session(item["device_id"]) or {}
                    item["state"] = s.get("state", "failed")
                    failed = failed or item["state"] != "succeeded"
                    running.remove(item)
                    self._publish(batch)
        states = {i["state"] for i in batch["items"]}
        batch["state"] = ("cancelled" if batch["_cancel"].is_set()
                          else "succeeded" if states == {"succeeded"} else "failed")
        batch["finished"] = time.time()
        ok = sum(i["state"] == "succeeded" for i in batch["items"])
        self._console.log("ota", f"Batch OTA {batch['state']}: {ok}/{len(batch['items'])} succeeded")
        self._publish(batch)

    def _publish(self, batch: dict) -> None:
        self._bus.publish({"type": "ota.batch", "batch": self._copy(batch)})
