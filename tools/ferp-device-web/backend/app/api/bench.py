"""Bench test runner (Test runner page) — admins only.

Runs the scripted hardware acceptance test (services/bench.py) on a device connected to this PC:
COM ports, firmware bundles, test checklist, presets, live run state and stored reports.
"""

import re
import time

from fastapi import Depends, HTTPException
from fastapi.responses import PlainTextResponse
from pydantic import BaseModel, Field

from ..container import Container
from ..services.bench import META, PUMP_TYPES
from .deps import admin_user, container, get_device, guard, protected

router = protected()

_PRESET_NAME = re.compile(r"^[\w .+-]{1,60}$")


class BenchSpec(BaseModel):
    device_id: str
    device_ip: str = ""
    device_port: str = Field(..., min_length=1)          # device UART, e.g. COM49
    emulator_port: str = ""                               # Mega display emulator, e.g. COM5
    main_fw_id: str = Field(..., min_length=1)            # main bundle the device must run
    dt_fw_id: str = ""                                    # DT bundle ("" = keep what the device has)
    pump_types: list[str] = []                            # emulator codes, e.g. ["CEN6", "WAY6"]
    sales: int = Field(2, ge=1, le=10)                    # sales per pump type
    pump_s: float = Field(6, ge=2, le=60)                 # pumping time per sale
    pumping_status: bool = False
    ota_test: bool = False
    ota_alt_fw_id: str = ""                               # the other (odd/even) main bundle
    uart_health: bool = True
    mismatch_test: bool = False                           # wrong display on the bus: device type x emulator type
    mismatch_device_types: list[str] = []                 # display types set on the device
    mismatch_emu_types: list[str] = []                    # emulator types played (each != the device type)
    mismatch_s: float = Field(20, ge=5, le=120)           # pumping data per pair
    preset: str = ""


class Preset(BaseModel):
    pump_types: list[str] = []
    sales: int = Field(2, ge=1, le=10)
    pump_s: float = Field(6, ge=2, le=60)
    pumping_status: bool = False
    ota_test: bool = False
    uart_health: bool = True
    mismatch_test: bool = False
    mismatch_device_types: list[str] = []
    mismatch_emu_types: list[str] = []
    mismatch_s: float = Field(20, ge=5, le=120)
    notes: str = ""


def _ports() -> list[dict]:
    try:
        from serial.tools import list_ports
        return [{"device": p.device, "description": p.description or ""} for p in sorted(list_ports.comports())]
    except Exception:
        return []


@router.get("/bench/meta", dependencies=[Depends(admin_user)])
def bench_meta(c: Container = Depends(container)):
    return {"pump_types": PUMP_TYPES, "ports": _ports(), "current": c.bench.current()}


@router.get("/bench/settings", dependencies=[Depends(admin_user)])
def get_settings(c: Container = Depends(container)):
    return c.docs.get(META, "settings") or {}


@router.put("/bench/settings", dependencies=[Depends(admin_user)])
def put_settings(body: dict, c: Container = Depends(container)):
    keep = {k: body.get(k) for k in ("device_id", "device_ip", "device_port", "emulator_port", "main_fw_id",
                                     "dt_fw_id", "ota_alt_fw_id", "preset") if k in body}
    c.docs.put(META, "settings", keep)
    return keep


@router.get("/bench/presets", dependencies=[Depends(admin_user)])
def list_presets(c: Container = Depends(container)):
    return (c.docs.get(META, "presets") or {}).get("presets", {})


@router.put("/bench/presets/{name}")
def save_preset(name: str, body: Preset, c: Container = Depends(container), user: str = Depends(admin_user)):
    if not _PRESET_NAME.match(name):
        raise HTTPException(400, "Preset name: 1-60 letters, digits, space, . + - _")
    bad = [p for p in body.pump_types + body.mismatch_device_types + body.mismatch_emu_types
           if p not in {t["code"] for t in PUMP_TYPES}]
    if bad:
        raise HTTPException(400, f"Unknown pump type(s): {', '.join(bad)}")
    doc = c.docs.get(META, "presets") or {"presets": {}}
    doc["presets"][name] = {**body.model_dump(), "saved": time.time(), "saved_by": user}
    c.docs.put(META, "presets", doc)
    return doc["presets"]


@router.delete("/bench/presets/{name}", dependencies=[Depends(admin_user)])
def delete_preset(name: str, c: Container = Depends(container)):
    doc = c.docs.get(META, "presets") or {"presets": {}}
    if doc["presets"].pop(name, None) is None:
        raise HTTPException(404, f"No preset {name}")
    c.docs.put(META, "presets", doc)
    return doc["presets"]


# ── manual pumping (Mega emulator commands) ─────────────────────────────────

_EMU_ARGS = {                       # verb → allowed arguments (None = no argument, "n" = whole number)
    "PING": {None}, "STATUS": {None}, "MENU": {None},
    "MODE": {t["code"] for t in PUMP_TYPES},
    "NOZZLE": {"1", "0", "AUTO"}, "PUMP": {"1", "0", "AUTO"},
    "PRICE": {"n"}, "VOLUME": {"n"}, "STEP": {"n"},
    "BUTTON": {"UP", "DOWN", "ENTER", "BACK"}, "DEBUG": {"1", "0"}, "EDGE": {"PUMP", "SIM", "SLOW"},
}


class EmulatorCmd(BaseModel):
    port: str = Field(..., min_length=1)
    cmd: str = Field(..., min_length=1, max_length=40)


class EmulatorSale(BaseModel):
    port: str = Field(..., min_length=1)
    pump_s: float = Field(6, ge=1, le=120)
    price: int | None = Field(None, ge=1, le=99999999)    # unit price x100; None = the mode's own price


class DeviceType(BaseModel):
    device_id: str
    display_type: int = Field(..., ge=1, le=8)
    reboot: bool = True


def _check_emu_cmd(cmd: str) -> str:
    parts = cmd.strip().upper().split()
    verb, arg = parts[0], (parts[1] if len(parts) > 1 else None)
    allowed = _EMU_ARGS.get(verb)
    if allowed is None or len(parts) > 2:
        raise HTTPException(400, f"Unknown emulator command '{cmd}' (allowed: {', '.join(_EMU_ARGS)})")
    if not (arg in allowed or ("n" in allowed and arg is not None and arg.isdigit())):
        raise HTTPException(400, f"{verb}: argument '{arg or ''}' not allowed")
    return " ".join(parts)


@router.post("/bench/emulator", dependencies=[Depends(admin_user)])
def emulator_cmd(body: EmulatorCmd, c: Container = Depends(container)):
    cmd = _check_emu_cmd(body.cmd)
    wait = 4.0 if cmd.startswith(("MODE", "MENU")) else 1.2      # a mode change restarts the emulator's driver
    return guard(c.bench.emulator_cmd, body.port, cmd, wait)


@router.post("/bench/emulator/sale", dependencies=[Depends(admin_user)])
def emulator_sale(body: EmulatorSale, c: Container = Depends(container)):
    return guard(c.bench.manual_sale, body.port, body.pump_s, body.price)


@router.post("/bench/device-type")
def device_type(body: DeviceType, c: Container = Depends(container), user: str = Depends(admin_user)):
    """Set the device's display type (config 0x6001) and optionally reboot it to apply it."""
    dev = get_device(c, body.device_id)
    verified = guard(c.ops.write_config, dev, 0x6001, str(body.display_type), user)
    if body.reboot:
        guard(c.ops.send, dev, "MsgSystemReboot", {})
    c.audit.record(user, "bench.device_type", dev, display_type=body.display_type, reboot=body.reboot)
    return {"verified": verified, "rebooted": body.reboot}


@router.post("/bench/runs")
def start_run(body: BenchSpec, c: Container = Depends(container), user: str = Depends(admin_user)):
    dev = get_device(c, body.device_id)
    if not dev.mqtt_id:
        raise HTTPException(400, f"Device '{dev.label}' has no MAC / UUID")
    if body.emulator_port and body.emulator_port.strip().upper() == body.device_port.strip().upper():
        raise HTTPException(400, "The device and the emulator need different COM ports")
    return guard(c.bench.start, body.model_dump(), dev, user)


@router.post("/bench/runs/cancel", dependencies=[Depends(admin_user)])
def cancel_run(c: Container = Depends(container)):
    return {"ok": c.bench.cancel()}


@router.get("/bench/runs", dependencies=[Depends(admin_user)])
def list_runs(c: Container = Depends(container)):
    return c.bench.runs()


@router.get("/bench/runs/{run_id}", dependencies=[Depends(admin_user)])
def get_run(run_id: str, c: Container = Depends(container)):
    r = c.bench.get_run(run_id)
    if not r:
        raise HTTPException(404, f"No test run {run_id}")
    return r


@router.get("/bench/runs/{run_id}/log", dependencies=[Depends(admin_user)], response_class=PlainTextResponse)
def run_log(run_id: str, c: Container = Depends(container)):
    return c.bench.run_log(run_id)


@router.delete("/bench/runs/{run_id}", dependencies=[Depends(admin_user)])
def delete_run(run_id: str, c: Container = Depends(container)):
    if not guard(c.bench.delete_run, run_id):
        raise HTTPException(404, f"No test run {run_id}")
    return {"ok": True}
