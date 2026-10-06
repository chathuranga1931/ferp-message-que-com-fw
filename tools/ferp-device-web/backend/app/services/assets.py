"""Customer sites (sheds), the wire harness catalogue and per-device history.

All three are JSON documents (DocumentStore → backend/data/docs/*.json):

    sheds.json        {id: Shed}            — keyed in practice by the shed *name*,
                                              which devices reference in their "shed" field
    harnesses.json    {SWH001: Harness}     — seeded with the harness list on first use
    device_log.json   {id: DeviceLogEntry}  — manufactured / delivered / repairs /
                                              modifications / issues / config / notes
"""

import time
import uuid
from datetime import date
from typing import Optional

from ..models import Device, DeviceIn, DeviceLogEntry, DeviceLogIn, HarnessIn, Shed, ShedIn
from ..ports import DeviceRepository, DocumentStore
from .audit import Audit

SHEDS, HARNESSES, LOG = "sheds", "harnesses", "device_log"

# Harness catalogue as of 2026-10 (SWH017–SWH031 reserved, not yet defined).
SEED_HARNESSES: list[tuple[str, str, str, bool]] = [
    ("SWH001", "TAP", "Display Capture cable for Hongyang", False),
    ("SWH002", "IO", "IO Cable for Hongyang (Two nozzles)", False),
    ("SWH003", "IO", "IO Cable for Censtar 12V Motor line Double Nozzle", False),
    ("SWH004", "IO", "IO Cable for WAYNE 24V Motor line Double Nozzle", False),
    ("SWH005", "TAP", "Display Capture cable for Censtar", False),
    ("SWH006", "TAP", "Display Capture cable for WAYNE (Retired, False pin mapping)", True),
    ("SWH007", "TAP", "IDC-Ribbon-10Pin-1to1-CenterTap-18Inch", False),
    ("SWH008", "TAP", "Display Capture cable for WAYNE", False),
    ("SWH009", "IO", "IO Cable for Hongyang (One nozzle)", False),
    ("SWH010", "TAP", "Display Capture Sanki", False),
    ("SWH011", "TAP", "Lanfent Display Tap Cable", False),
    ("SWH012", "IO", "Single Nozzle IO Cable for Censtar, Half of SWH003", False),
    ("SWH013", "IO", "Censtar, Improved version with connectable externals, will replace SWH003, 012 etc", False),
    ("SWH014", "TAP", "ESP32 adaptor cable for Censtar high speed", False),
    ("SWH015", "TAP", "ESP32 adapter cable for Langfeng High speed", False),
    ("SWH016", "IO", "Censtar Single Nozzle, Improved version with connectable externals, will replace SWH003, 012 etc", False),
    *[(f"SWH{n:03d}", "", "", False) for n in range(17, 32)],
    ("SWH050", "Suppliment", "Steel 19mm Button cable 60cm", False),
    ("SWH051", "Suppliment", "Steel 19mm Button cable 20cm", False),
    ("SWH052", "Suppliment", "Buzzer 12V cable for FERP Com board", False),
    ("SWH053", "Suppliment", "Motor tap 12V adapter for SWH013 and SWH016 IO cables", False),
]


def _norm(name: str) -> str:
    return name.strip().lower()


class Assets:
    def __init__(self, docs: DocumentStore, devices: DeviceRepository, audit: Audit):
        self._docs, self._devices, self._audit = docs, devices, audit

    # ── sheds ─────────────────────────────────────────────────────────────────

    def sheds(self) -> list[dict]:
        return sorted(self._docs.list(SHEDS), key=lambda s: s["name"].lower())

    def shed(self, sid: str) -> Optional[dict]:
        return self._docs.get(SHEDS, sid)

    def shed_by_name(self, name: str) -> Optional[dict]:
        n = _norm(name)
        return next((s for s in self._docs.list(SHEDS) if _norm(s["name"]) == n), None) if n else None

    def save_shed(self, user: str, body: ShedIn, sid: Optional[str] = None) -> dict:
        other = self.shed_by_name(body.name)
        if other and other["id"] != sid:
            raise ValueError(f"A shed named '{body.name}' already exists")
        old = self.shed(sid) if sid else None
        if sid and old is None:
            raise KeyError(f"Unknown shed {sid}")
        shed = Shed(id=sid or uuid.uuid4().hex[:10], **body.model_dump()).model_dump()
        self._docs.put(SHEDS, shed["id"], shed)
        renamed = 0
        if old and _norm(old["name"]) != _norm(body.name):
            # keep devices attached: their "shed" field holds the name
            for d in self._devices.list():
                if _norm(d.shed) == _norm(old["name"]):
                    self._devices.update(d.id, DeviceIn(**{**d.model_dump(exclude={"id"}), "shed": body.name}))
                    renamed += 1
        self._audit.record(user, "shed.save", name=body.name, renamed_devices=renamed)
        return {**shed, "renamed_devices": renamed}

    def delete_shed(self, user: str, sid: str) -> bool:
        shed = self.shed(sid)
        ok = self._docs.delete(SHEDS, sid)
        if ok:
            self._audit.record(user, "shed.delete", name=shed["name"] if shed else sid)
        return ok

    # ── harness catalogue ─────────────────────────────────────────────────────

    def harnesses(self) -> list[dict]:
        items = self._docs.list(HARNESSES)
        if not items:
            for hid, typ, desc, retired in SEED_HARNESSES:
                self._docs.put(HARNESSES, hid, HarnessIn(id=hid, type=typ, description=desc, retired=retired).model_dump())
            items = self._docs.list(HARNESSES)
        usage: dict[str, int] = {}
        for d in self._devices.list():
            for h in d.harnesses:
                usage[h] = usage.get(h, 0) + 1
        return sorted(({**h, "in_use": usage.get(h["id"], 0)} for h in items), key=lambda h: h["id"])

    def save_harness(self, user: str, body: HarnessIn) -> dict:
        h = body.model_dump()
        self._docs.put(HARNESSES, body.id, h)
        self._audit.record(user, "harness.save", id=body.id)
        return h

    def delete_harness(self, user: str, hid: str) -> bool:
        if any(hid in d.harnesses for d in self._devices.list()):
            raise ValueError(f"{hid} is still fitted to devices — remove it from them first")
        ok = self._docs.delete(HARNESSES, hid)
        if ok:
            self._audit.record(user, "harness.delete", id=hid)
        return ok

    # ── device history ────────────────────────────────────────────────────────

    def log(self, device_id: str) -> list[dict]:
        items = [e for e in self._docs.list(LOG) if e["device_id"] == device_id]
        return sorted(items, key=lambda e: (e.get("date") or "", e["created"]), reverse=True)

    def all_log(self) -> list[dict]:
        return self._docs.list(LOG)

    def add_log(self, user: str, dev: Device, body: DeviceLogIn) -> dict:
        data = body.model_dump()
        if not data["date"]:
            data["date"] = date.today().isoformat()
        if data["kind"] == "issue" and not data["status"]:
            data["status"] = "open"
        entry = DeviceLogEntry(id=uuid.uuid4().hex[:10], device_id=dev.id, created=time.time(), user=user,
                               **data).model_dump()
        self._docs.put(LOG, entry["id"], entry)
        self._apply_side_effects(dev, entry)
        self._audit.record(user, "device.log.add", dev, kind=entry["kind"], title=entry["title"])
        return entry

    def update_log(self, user: str, eid: str, body: DeviceLogIn) -> dict:
        old = self._docs.get(LOG, eid)
        if old is None:
            raise KeyError(f"Unknown log entry {eid}")
        entry = {**old, **body.model_dump()}
        if not entry["date"]:
            entry["date"] = old["date"]
        self._docs.put(LOG, eid, entry)
        dev = self._devices.get(entry["device_id"])
        if dev:
            self._apply_side_effects(dev, entry)
        self._audit.record(user, "device.log.update", dev, kind=entry["kind"], title=entry["title"])
        return entry

    def delete_log(self, user: str, eid: str) -> bool:
        old = self._docs.get(LOG, eid)
        ok = self._docs.delete(LOG, eid)
        if ok and old:
            self._audit.record(user, "device.log.delete", self._devices.get(old["device_id"]), title=old["title"])
        return ok

    def _apply_side_effects(self, dev: Device, entry: dict) -> None:
        """Manufactured / delivered / config entries keep the device's own fields current."""
        changes = {}
        if entry["kind"] == "manufactured" and not dev.manufactured:
            changes["manufactured"] = entry["date"]
        elif entry["kind"] == "delivered" and not dev.delivered:
            changes["delivered"] = entry["date"]
        elif entry["kind"] == "config" and entry.get("snapshot_id"):
            changes["config_snapshot"] = entry["snapshot_id"]
        if changes:
            self._devices.update(dev.id, DeviceIn(**{**dev.model_dump(exclude={"id"}), **changes}))

    def record_snapshot_applied(self, user: str, devices: list[Device], snap: dict, results: list[dict]) -> None:
        """Called after a snapshot was applied: log it and mark it as the device's loaded config."""
        by_id = {r["device_id"]: r for r in results}
        for dev in devices:
            r = by_id.get(dev.id)
            if r is None:
                continue
            keys = r.get("keys", 0)
            self.add_log(user, dev, DeviceLogIn(
                kind="config", title=f"Config snapshot \"{snap['name']}\" applied",
                details=f"{keys} key(s) written" if keys else "Device already matched the snapshot",
                snapshot_id=snap["id"]))
