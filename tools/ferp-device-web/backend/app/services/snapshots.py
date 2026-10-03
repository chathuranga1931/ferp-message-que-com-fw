"""
Config snapshots: a named set of config key values, taken from a device's
last-read config (or imported), compared in the UI, and applied to one or
more devices as ordinary config.write jobs (only differing keys by default).

Document shape (collection "snapshots"):
    {id, name, notes, created, created_by, source: {device_id, label} | null,
     values: {"0x1001": {"name": "WIFI_SSID", "type": "STRING", "value": "..."}}}
"""

import time
import uuid
from typing import Optional

from ferp_core.transports import TYPE_NAMES

from ..models import Device
from ..ports import DocumentStore
from .audit import Audit
from .catalog import Catalog
from .device_ops import DeviceOps

_COLLECTION = "snapshots"


class Snapshots:
    def __init__(self, docs: DocumentStore, ops: DeviceOps, catalog: Catalog, audit: Audit):
        self._docs, self._ops, self._catalog, self._audit = docs, ops, catalog, audit

    def list(self) -> list[dict]:
        return sorted(self._docs.list(_COLLECTION), key=lambda s: s.get("created", 0), reverse=True)

    def get(self, sid: str) -> Optional[dict]:
        return self._docs.get(_COLLECTION, sid)

    def create(self, user: str, name: str, notes: str = "", device: Optional[Device] = None,
               values: Optional[dict[str, str]] = None) -> dict:
        snap_values: dict[str, dict] = {}
        if device is not None:
            cached = self._ops.cached_config(device)
            if not cached:
                raise ValueError("No config has been read from this device yet — use Read all first")
            for key_str, entry in cached.items():
                ck = self._catalog.config_key(int(key_str))
                snap_values[f"0x{int(key_str):04X}"] = {
                    "name": ck.name if ck else "", "type": entry.get("value_type", ""), "value": entry["value"]}
        elif values:
            for k, v in values.items():
                key_id = int(k, 16) if isinstance(k, str) and k.lower().startswith("0x") else int(k)
                ck = self._catalog.config_key(key_id)
                if ck is None:
                    raise ValueError(f"Unknown config key {k}")
                snap_values[f"0x{key_id:04X}"] = {"name": ck.name, "type": TYPE_NAMES.get(ck.type_id, ""), "value": str(v)}
        else:
            raise ValueError("Give a device_id or values")

        snap = {"id": uuid.uuid4().hex[:10], "name": name, "notes": notes, "created": time.time(),
                "created_by": user, "source": {"device_id": device.id, "label": device.label} if device else None,
                "values": dict(sorted(snap_values.items()))}
        self._docs.put(_COLLECTION, snap["id"], snap)
        self._audit.record(user, "snapshot.create", device, snapshot=name, keys=len(snap_values))
        return snap

    def delete(self, user: str, sid: str) -> bool:
        snap = self.get(sid)
        ok = self._docs.delete(_COLLECTION, sid)
        if ok:
            self._audit.record(user, "snapshot.delete", snapshot=snap.get("name") if snap else sid)
        return ok

    def apply(self, user: str, sid: str, devices: list[Device], keys: Optional[list[int]],
              only_diff: bool) -> list[dict]:
        snap = self.get(sid)
        if snap is None:
            raise KeyError(f"Unknown snapshot {sid}")
        wanted = {int(k, 16): v["value"] for k, v in snap["values"].items()
                  if keys is None or int(k, 16) in keys}
        results = []
        for dev in devices:
            cached = self._ops.cached_config(dev) if only_diff else {}
            todo = [(k, v) for k, v in wanted.items()
                    if not only_diff or cached.get(str(k), {}).get("value") != v]
            if not todo:
                results.append({"device_id": dev.id, "job_id": None, "keys": 0})
                continue
            job_id = self._ops.start_write_config(dev, todo, user=user)
            results.append({"device_id": dev.id, "job_id": job_id, "keys": len(todo)})
            self._audit.record(user, "snapshot.apply", dev, snapshot=snap["name"], keys=len(todo))
        return results
