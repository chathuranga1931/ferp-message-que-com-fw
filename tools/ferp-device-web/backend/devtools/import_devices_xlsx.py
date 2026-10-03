"""
import_devices_xlsx.py — add devices from the FERP device spreadsheet ("Devices" sheet)
to the web app's device registry (backend/data/devices.json).

    python devtools/import_devices_xlsx.py <FERP-Device-List.xlsx> [--dry-run] [--data-dir backend/data]

Rules
  * Existing registry entries are never changed. A sheet row is skipped when its MAC
    (or, without a MAC, its Board ID) is already in the registry — so re-running only adds new rows.
  * Board ID → label · MAC → mac (colons removed, lower-case) · PCB Version → board_version
    SD Card Size → sd_card_size · Filling Station Name → shed ("Unassigned" when empty)
    Pump Type → pump_type · Assigned Nozzle1/2 → pump_id_1/2
    Type → device_type: COM → "COM", PRN / PRN-MOD → "Printer" (blank: from the Board ID prefix)
    Type, District, fuel types and sheet notes go into notes.
  * Rows without a Board ID are skipped (reported). "N/A" / "-" count as empty.
  * A backup of devices.json is written next to it before saving.
"""

import argparse
import json
import re
import shutil
import sys
import time
import uuid
from pathlib import Path

BACKEND = Path(__file__).resolve().parent.parent
EMPTY = {"", "n/a", "na", "-", "--", "none", "null"}


def cell(row, ix, name):
    i = ix.get(name)
    v = row[i] if i is not None and i < len(row) else None
    if v is None:
        return ""
    if isinstance(v, float) and v.is_integer():
        v = int(v)                                   # 2404.0 → 2404
    s = str(v).strip()
    return "" if s.lower() in EMPTY else s


def device_type(sheet_type: str, board_id: str) -> str:
    t = sheet_type.upper()
    if t.startswith("PRN") or t.startswith("PTR"):
        return "Printer"
    if t == "COM":
        return "COM"
    b = board_id.upper()
    if "-PRN-" in b or "-PTR-" in b:
        return "Printer"
    if "-COM-" in b:
        return "COM"
    return sheet_type


def norm_mac(m: str) -> str:
    return re.sub(r"[^0-9a-fA-F]", "", m).lower()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("xlsx", type=Path)
    ap.add_argument("--sheet", default="Devices")
    ap.add_argument("--data-dir", type=Path, default=BACKEND / "data")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    import openpyxl
    wb = openpyxl.load_workbook(a.xlsx, data_only=True, read_only=True)
    sheet = next((s for s in wb.sheetnames if s.lower() == a.sheet.lower()), None)
    if not sheet:
        print(f"Sheet {a.sheet!r} not found; sheets: {wb.sheetnames}")
        return 1
    rows = list(wb[sheet].iter_rows(values_only=True))
    ix = {str(h).strip(): i for i, h in enumerate(rows[0]) if h is not None}

    reg_path = a.data_dir / "devices.json"
    registry = json.loads(reg_path.read_text(encoding="utf-8")) if reg_path.exists() else {"devices": []}
    devices = registry.setdefault("devices", [])
    # only entries that were in the registry before this run count as "existing";
    # rows sharing a MAC inside the sheet are all added (and flagged)
    known_macs = {norm_mac(d.get("mac", "")) for d in devices if norm_mac(d.get("mac", ""))}
    known_labels = {d.get("label", "").strip().lower() for d in devices}

    sheet_macs: dict[str, list[str]] = {}
    for r in rows[1:]:
        m = norm_mac(cell(r, ix, "MAC Address"))
        if len(m) == 12 and cell(r, ix, "Board ID"):
            sheet_macs.setdefault(m, []).append(cell(r, ix, "Board ID"))

    added, skipped_existing, skipped_noid, warnings = [], [], [], []
    for n, r in enumerate(rows[1:], start=2):
        if not any(c not in (None, "") for c in r[:16]):
            continue
        label = cell(r, ix, "Board ID")
        raw_mac = cell(r, ix, "MAC Address")
        mac = norm_mac(raw_mac)
        if not label:
            skipped_noid.append(f"row {n}: {raw_mac or '(no MAC)'} {cell(r, ix, 'Type')} {cell(r, ix, 'Filling Station Name')}".strip())
            continue
        if (len(mac) == 12 and mac in known_macs) or label.lower() in known_labels:
            skipped_existing.append(f"{label} ({mac or 'no MAC'})")
            continue

        notes = []
        if cell(r, ix, "Type"):
            notes.append(f"Type: {cell(r, ix, 'Type')}")
        if cell(r, ix, "District"):
            notes.append(f"District: {cell(r, ix, 'District')}")
        for k, lbl in (("N1 Fuel Type", "N1"), ("N2 Fuel Type", "N2")):
            if cell(r, ix, k):
                notes.append(f"{lbl}: {cell(r, ix, k)}")
        if len(mac) != 12:
            notes.append(f"MAC in sheet: {raw_mac or '(empty)'}")
            warnings.append(f"{label}: no usable MAC ({raw_mac or 'empty'}) — added without MAC")
            mac = ""
        elif len(sheet_macs.get(mac, [])) > 1:
            others = [x for x in sheet_macs[mac] if x != label]
            notes.append(f"Same MAC as {', '.join(others)} in sheet")
            warnings.append(f"{label}: MAC {mac} also used by {', '.join(others)}")
        if cell(r, ix, "Notes"):
            notes.append(cell(r, ix, "Notes"))

        dev = {
            "id": uuid.uuid4().hex[:8], "label": label, "mac": mac, "uuid": "", "group": "default", "ip": "",
            "notes": " · ".join(notes),
            "device_type": device_type(cell(r, ix, "Type"), label),
            "shed": cell(r, ix, "Filling Station Name") or "Unassigned",
            "pump_id_1": cell(r, ix, "Assigned Nozzle1"),
            "pump_id_2": cell(r, ix, "Assigned Nozzle2"),
            "sd_card_size": cell(r, ix, "SD Card Size"),
            "board_version": cell(r, ix, "PCB Version"),
            "pump_type": cell(r, ix, "Pump Type"),
        }
        devices.append(dev)
        added.append(dev)
        known_labels.add(label.lower())

    print(f"Sheet rows with a Board ID: {len(added) + len(skipped_existing)}")
    print(f"  add:               {len(added)}  (shed 'Unassigned': {sum(d['shed'] == 'Unassigned' for d in added)})")
    print(f"  already in registry (left unchanged): {len(skipped_existing)}: {', '.join(skipped_existing)}")
    print(f"Skipped rows without a Board ID: {len(skipped_noid)}")
    for s in skipped_noid:
        print(f"  {s}")
    if warnings:
        print("Warnings:")
        for w in warnings:
            print(f"  {w}")
    if a.dry_run:
        print("\nDry run — nothing written. Sample:")
        for d in added[:3]:
            print("  ", json.dumps({k: v for k, v in d.items() if k != "id"}, ensure_ascii=False))
        return 0

    if reg_path.exists():
        backup = reg_path.with_name(f"devices.backup-{time.strftime('%Y%m%d-%H%M%S')}.json")
        shutil.copy2(reg_path, backup)
        print(f"\nBackup: {backup}")
    tmp = reg_path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(registry, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp.replace(reg_path)
    print(f"Wrote {reg_path} — {len(devices)} devices")
    return 0


if __name__ == "__main__":
    sys.exit(main())
