"""
Bench test runner (admin "Test runner" page) — a scripted hardware acceptance test, no AI involved.

A run drives one bench device with the same services the web app uses (MqttHub / DeviceOps for
commands and events, OtaManager + FirmwareLibrary for OTA) plus two serial ports on this PC:
the device's UART (log, shown live and stored) and the Mega display emulator.

Steps (each is one report item):
  identity       the device answers over MQTT, its MAC is the selected device's, the UART on the
                 given COM port belongs to it, the local IP answers a ping       (abort on failure)
  firmware       the bundles fit the board; when the device does not run the selected main / DT
                 versions they are installed by OTA, followed by 2 reboots, and the versions are
                 read back                                                         (abort on failure)
  pump:<code>    per ticked pump type: emulator mode, display type, 2 reboots after a change,
                 frames of the type must arrive, then N sales on both nozzles = emulator values
  pumping_status MsgPumpingStatus every interval while pumping, rising volume, both nozzles
  ota_roundtrip  main → alternate bundle → main, the reported version checked after each
  uart_health    no crash / watchdog / brown-out reset in the UART log of the whole run

Events: bench.state {run}, bench.log {run_id, lines:[{ts, src, text}]}.
Storage: docs "bench_runs"/<run_id> (report), blob "bench/<run_id>.log" (full log),
         docs "bench"/"settings" (last form), docs "bench"/"presets".
"""

import logging
import platform
import re
import subprocess
import threading
import time
from datetime import datetime
from typing import Callable, Optional

from ferp_core import topics

from ..models import AppConfig, Device
from ..ports import BlobStore, DocumentStore, EventBus
from .audit import Audit
from .device_ops import DeviceOps
from .mqtt_hub import MqttHub
from .ota import FirmwareLibrary, OtaManager, bundle_fits_board

log = logging.getLogger("ferp.bench")

RUNS = "bench_runs"
META = "bench"

# Pump types the Mega emulator can play: code (emulator MODE), device display type, emulator unit
# price x100 to set (None = the mode's own price).
PUMP_TYPES = [
    {"code": "CEN6", "type": 1, "name": "Censtar 6-digit",   "price": 42300},
    {"code": "CEN7", "type": 2, "name": "Censtar 7-digit",   "price": None},
    {"code": "C7CS", "type": 3, "name": "Censtar 7-digit CS", "price": None},
    {"code": "HY8",  "type": 4, "name": "Hongyang 8-digit",  "price": None},
    {"code": "WAY6", "type": 5, "name": "Wayne 6-digit",     "price": None},
    {"code": "SAN6", "type": 6, "name": "Sanki 6-digit",     "price": 42300},
    {"code": "LF8",  "type": 7, "name": "Longfeng 8-digit",  "price": None},
    {"code": "WAY2", "type": 8, "name": "Wayne 2",           "price": None},
]
_PT = {p["code"]: p for p in PUMP_TYPES}

KEY_FW, KEY_HW, KEY_MAC, KEY_DT = 0xA004, 0xA005, 0xA003, 0xA006
KEY_DISPLAY_TYPE, KEY_DT_LOG_RATE = 0x6001, 0x8004
KEY_PUMP_STATUS_EN, KEY_PUMP_STATUS_INT = 0x4005, 0x4006
BENCH_DT_LOG_RATE = 120          # frame log lines / min during a run: frames show the DT's running type
MISMATCH_MAX_LINES_S = 30         # wrong-display test: average UART lines/s above this = log flood
MISMATCH_MAX_PEAK_S = 80          # ... or this many lines in a single second

_RE_FRAME = re.compile(r"\[[01]\]\s+(\d+)\s")       # display type per nozzle in a "Received frame:" line
_RE_RESET = re.compile(r"rst:0x([0-9a-f]+) \(([A-Z_0-9]+)\)")
_RE_IP = re.compile(r"GOT_IP ip=([0-9.]+)")
_RE_EMU = re.compile(r"vol_ml=(\d+) price=(\d+) total=(\d+)")
_CRASH = ("Guru Meditation", "abort() was called", "Backtrace:", "Brownout", "stack overflow", "assert failed")
_OK_RESETS = {"SW_CPU_RESET", "POWERON_RESET"}       # our reboots / OTA restarts, power-up


class Abort(Exception):
    """Stops the run (cancelled, or a step that later steps depend on failed)."""


class SerialTap:
    """Reads a serial port line by line in a thread; keeps every line of the run."""

    def __init__(self, port: str, baud: int, src: str, on_line: Callable[[str, float, str], None]):
        import serial                                   # pyserial
        self._s = serial.Serial()
        self._s.port, self._s.baudrate, self._s.timeout = port, baud, 0.2
        self._s.dtr = False                             # keep DTR/RTS low: they reset the ESP32 / Mega
        self._s.rts = False
        try:
            self._s.open()
        except Exception as exc:
            raise Abort(f"{port}: cannot open ({exc}) — is another program (logger, serial monitor) using it?")
        self.port, self.src, self._on_line = port, src, on_line
        self.lines: list[tuple[float, str]] = []
        self._lock = threading.Lock()
        self._stop = False
        self._t = threading.Thread(target=self._read, name=f"bench-{port}", daemon=True)
        self._t.start()

    def _read(self) -> None:
        buf = b""
        while not self._stop:
            try:
                chunk = self._s.read(4096)
            except Exception as exc:
                self._on_line(self.src, time.time(), f"[{self.port} read error: {exc}]")
                return
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").rstrip("\r")
                ts = time.time()
                with self._lock:
                    self.lines.append((ts, text))
                self._on_line(self.src, ts, text)

    def write(self, text: str) -> None:
        self._s.write(text.encode())

    def mark(self) -> int:
        with self._lock:
            return len(self.lines)

    def since(self, mark: int) -> list[str]:
        with self._lock:
            return [t for _, t in self.lines[mark:]]

    def since_ts(self, mark: int) -> list[tuple[float, str]]:
        with self._lock:
            return list(self.lines[mark:])

    def close(self) -> None:
        self._stop = True
        self._t.join(timeout=2)
        try:
            self._s.close()
        except Exception:
            pass


class BenchRunner:
    def __init__(self, hub: MqttHub, ops: DeviceOps, ota: OtaManager, firmware: FirmwareLibrary,
                 docs: DocumentStore, blobs: BlobStore, bus: EventBus, get_config: Callable[[], AppConfig],
                 audit: Optional[Audit] = None):
        self._hub, self._ops, self._ota, self._fw = hub, ops, ota, firmware
        self._docs, self._blobs, self._bus, self._cfg, self._audit = docs, blobs, bus, get_config, audit
        self._lock = threading.Lock()
        self._emu_lock = threading.Lock()          # one manual emulator command at a time
        self._run: Optional[dict] = None           # the running run (report document)
        self._cancel = threading.Event()
        self._events: list[tuple[float, str, dict]] = []    # MQTT events of the bench device (ts, msg, data)
        self._topic = ""
        self._pending: list[dict] = []               # log lines not yet sent to the browser
        self._pending_lock = threading.Lock()
        self._log_lines: list[str] = []              # full run log (stored as a blob at the end)
        hub.add_listener(self._on_mqtt)
        threading.Thread(target=self._flush_loop, name="bench-flush", daemon=True).start()

    # ── public API ────────────────────────────────────────────────────────────

    def current(self) -> Optional[dict]:
        with self._lock:
            return dict(self._run) if self._run else None

    def runs(self) -> list[dict]:
        out = [{k: v for k, v in r.items() if k != "items"} | {"items_total": len(r.get("items", []))}
               for r in self._docs.list(RUNS)]
        return sorted(out, key=lambda r: r.get("started", 0), reverse=True)

    def get_run(self, run_id: str) -> Optional[dict]:
        cur = self.current()
        if cur and cur["id"] == run_id:
            return cur
        return self._docs.get(RUNS, run_id)

    def run_log(self, run_id: str) -> str:
        cur = self.current()
        if cur and cur["id"] == run_id:
            return "\n".join(self._log_lines)
        try:
            return self._blobs.get(f"bench/{run_id}.log").decode("utf-8", "replace")
        except KeyError:
            return ""

    def delete_run(self, run_id: str) -> bool:
        cur = self.current()
        if cur and cur["id"] == run_id:
            raise RuntimeError("The run is still in progress — cancel it first")
        try:
            self._blobs.delete(f"bench/{run_id}.log")
        except KeyError:
            pass
        return self._docs.delete(RUNS, run_id)

    def cancel(self) -> bool:
        if not self.current():
            return False
        self._cancel.set()
        return True

    def start(self, spec: dict, dev: Device, user: str) -> dict:
        with self._lock:
            if self._run and self._run["state"] == "running":
                raise RuntimeError(f"A test run is already in progress ({self._run['id']})")
            if not self._hub.connected:
                raise RuntimeError("MQTT is not connected")
            main = self._fw.meta(spec["main_fw_id"]) if spec.get("main_fw_id") else None
            if not main:
                raise ValueError("Select the main firmware bundle")
            dt = self._fw.meta(spec["dt_fw_id"]) if spec.get("dt_fw_id") else None
            if spec.get("dt_fw_id") and not dt:
                raise ValueError("The selected DT bundle is not in the library any more")
            alt = self._fw.meta(spec["ota_alt_fw_id"]) if spec.get("ota_alt_fw_id") else None
            if spec.get("ota_test") and not alt:
                raise ValueError("The OTA test needs the second (odd/even) main bundle")
            if alt and alt["id"] == main["id"]:
                raise ValueError("The OTA test needs a different bundle than the main firmware")
            bad = [c for c in spec.get("pump_types", []) + spec.get("mismatch_device_types", [])
                   + spec.get("mismatch_emu_types", []) if c not in _PT]
            if bad:
                raise ValueError(f"Unknown pump type(s): {', '.join(bad)}")
            if spec.get("mismatch_test") and not any(e != d for d in spec.get("mismatch_device_types", [])
                                                      for e in spec.get("mismatch_emu_types", [])):
                raise ValueError("Wrong-display test: tick at least one device type and one other emulator type")
            now = time.time()
            run_id = datetime.fromtimestamp(now).strftime("%Y%m%d-%H%M%S")
            self._run = {
                "id": run_id, "tag": run_id, "state": "running", "user": user, "started": now, "finished": None,
                "device": {"id": dev.id, "label": dev.label, "mac": dev.mqtt_id},
                "spec": spec, "bundles": {"main": _short(main), "dt": _short(dt), "alt": _short(alt)},
                "versions": {}, "items": [], "summary": "", "preset": spec.get("preset", ""),
            }
            self._cancel.clear()
            self._events = []
            self._topic = topics.topic_id(dev.mqtt_id)
            self._log_lines = []
        if self._audit:
            self._audit.record(user, "bench.start", dev, run=run_id, main=main["version"], dt=dt and dt["version"])
        threading.Thread(target=self._main, args=(spec, dev, main, dt, alt), name="bench-run", daemon=True).start()
        self._publish()
        return self.current()

    # ── manual emulator control (Manual pumping section) ─────────────────────

    def _port_in_use(self, port: str) -> bool:
        cur = self.current()
        if not cur or cur["state"] != "running":
            return False
        spec = cur["spec"]
        return port.upper() in {(spec.get("emulator_port") or "").upper(), (spec.get("device_port") or "").upper()}

    def emulator_cmd(self, port: str, cmd: str, wait: float = 1.5) -> dict:
        """Send one command to the Mega emulator; returns {cmd, reply: [lines], ok}."""
        return self.emulator_script(port, [(cmd, wait)])[0]

    def emulator_script(self, port: str, steps: list[tuple[str, float]]) -> list[dict]:
        """Send commands in order over one connection; after each, collect replies for `wait` s."""
        import serial
        port = port.strip().upper()
        if not port:
            raise ValueError("Set the emulator COM port")
        if self._port_in_use(port):
            raise RuntimeError(f"{port} is in use by the running test — cancel it first")
        with self._emu_lock:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 0.1
            s.dtr = False                               # DTR/RTS low: opening must not reset the Mega
            s.rts = False
            try:
                s.open()
            except Exception as exc:
                raise RuntimeError(f"{port}: cannot open ({exc}) — is another program using it?")
            out = []
            try:
                s.reset_input_buffer()
                for cmd, wait in steps:
                    s.write((cmd + "\n").encode())
                    end, buf = time.time() + wait, b""
                    while time.time() < end:
                        buf += s.read(4096)
                    reply = [l.strip() for l in buf.decode("utf-8", "replace").splitlines() if l.strip()]
                    out.append({"cmd": cmd, "reply": reply, "ok": any(l.startswith("OK") for l in reply)
                                and not any(l.startswith("ERR") for l in reply)})
            finally:
                s.close()
        return out

    def manual_sale(self, port: str, pump_s: float, price: Optional[int]) -> dict:
        """One sale from the emulator: nozzle up, pump for pump_s, stop, read STATUS, nozzle down."""
        steps = ([(f"PRICE {price}", 0.5)] if price else []) + [
            ("NOZZLE 1", 3), ("PUMP 1", pump_s), ("PUMP 0", 2), ("STATUS", 0.6), ("NOZZLE 0", 3),
            ("NOZZLE AUTO", 0.4), ("PUMP AUTO", 0.4)]
        out = self.emulator_script(port, steps)
        st = next((r for r in out if r["cmd"] == "STATUS"), {"reply": []})
        m = next((x for x in (_RE_EMU.search(l) for l in st["reply"]) if x), None)
        sale = {"vol_l": int(m.group(1)) / 1000, "price": int(m.group(2)) / 100, "total": int(m.group(3)) / 100} if m else None
        return {"steps": out, "sale": sale}

    # ── run ───────────────────────────────────────────────────────────────────

    def _main(self, spec: dict, dev: Device, main: dict, dt: Optional[dict], alt: Optional[dict]) -> None:
        self.uart: Optional[SerialTap] = None
        self.emu: Optional[SerialTap] = None
        restore: dict[int, str] = {}
        state = "error"
        try:
            self._say(f"Test run {self._run['id']} on {dev.label} ({dev.mqtt_id}) — main {main['version']}"
                      + (f", DT {dt['version']}" if dt else ", DT: keep"))
            self.uart = SerialTap(spec["device_port"], 115200, "uart", self._on_serial)
            self._say(f"Device UART: {spec['device_port']}")
            needs_emu = bool(spec.get("pump_types") or spec.get("pumping_status") or spec.get("mismatch_test"))
            if needs_emu:
                if not spec.get("emulator_port"):
                    raise Abort("Pump tests need the emulator COM port")
                self.emu = SerialTap(spec["emulator_port"], 115200, "emu", self._on_serial)
                self._say(f"Emulator: {spec['emulator_port']}")

            self._step("identity", "Device identity (MQTT, MAC, COM port, local IP)", True,
                       lambda: self._identity(dev, spec))
            self._step("firmware", "Firmware versions (OTA when different)", True,
                       lambda: self._firmware(dev, main, dt))
            v = self._run["versions"]
            self._run["tag"] = f"{self._run['id']}-{v.get('main', '?')}-{v.get('dt', '?')}"
            self._publish()

            if needs_emu:
                restore[KEY_DT_LOG_RATE] = self._cfg_read(dev, KEY_DT_LOG_RATE)
                self._ops.write_config(dev, KEY_DT_LOG_RATE, str(BENCH_DT_LOG_RATE), user="bench")
                self._say(f"DT frame log rate set to {BENCH_DT_LOG_RATE}/min for the run "
                          f"(was {restore[KEY_DT_LOG_RATE]}, restored at the end)")
            for code in spec.get("pump_types", []):
                p = _PT[code]
                self._step(f"pump:{code}", f"Pump type {code} — {p['name']} (type {p['type']})", False,
                           lambda p=p: self._pump_type(dev, p, int(spec.get("sales", 2)), float(spec.get("pump_s", 6))))
            if spec.get("mismatch_test"):
                secs = float(spec.get("mismatch_s", 20))
                for dc in spec.get("mismatch_device_types", []):
                    for ec in spec.get("mismatch_emu_types", []):
                        if ec == dc:
                            continue
                        self._step(f"mismatch:{dc}:{ec}", f"Wrong display — device set to {dc}, emulator plays {ec}", False,
                                   lambda d=_PT[dc], e=_PT[ec]: self._mismatch(dev, d, e, secs))
            if spec.get("pumping_status"):
                restore[KEY_PUMP_STATUS_EN] = self._cfg_read(dev, KEY_PUMP_STATUS_EN)
                restore[KEY_PUMP_STATUS_INT] = self._cfg_read(dev, KEY_PUMP_STATUS_INT)
                self._step("pumping_status", "Pumping status uplink while pumping", False,
                           lambda: self._pumping_status(dev))
            if spec.get("ota_test"):
                self._step("ota_roundtrip", f"OTA {main['version']} → {alt['version']} → {main['version']}", False,
                           lambda: self._ota_roundtrip(dev, main, alt))
            if spec.get("uart_health", True):
                self._step("uart_health", "UART log: no crash or unexpected reset", False, self._uart_health)
            failed = [i for i in self._run["items"] if i["result"] == "fail"]
            state = "failed" if failed else "passed"
        except Abort as exc:
            state = "aborted" if self._cancel.is_set() else "failed"
            self._say(f"Run stopped: {exc}", "error")
        except Exception as exc:
            log.exception("bench run crashed")
            self._say(f"Run error: {exc}", "error")
            state = "error"
        finally:
            # Clean-up must never be cancelled (a set Cancel flag would abort its waits) and must
            # always end with _finish(): otherwise the run stays "running" and keeps the COM ports.
            try:
                for key, value in restore.items():
                    try:
                        if value is not None and value != "":
                            self._ops.write_config(dev, key, str(value), user="bench")
                    except Exception as exc:
                        self._say(f"Could not restore config 0x{key:04X}={value}: {exc}", "warn")
                if self.emu:
                    for c in ("NOZZLE AUTO", "PUMP AUTO"):
                        try:
                            self._emu(c, 0.3, cancellable=False)
                        except Exception as exc:
                            self._say(f"Emulator {c}: {exc}", "warn")
            finally:
                for tap in (self.uart, self.emu):
                    if tap:
                        try:
                            tap.close()
                        except Exception:
                            log.exception("closing %s failed", tap.port)
                self._finish(state)

    def _finish(self, state: str) -> None:
        r = self._run
        items = r["items"]
        counts = {k: sum(1 for i in items if i["result"] == k) for k in ("pass", "fail", "skip")}
        r["state"], r["finished"] = state, time.time()
        r["summary"] = (f"{state.upper()} — {counts['pass']} passed, {counts['fail']} failed"
                        + (f", {counts['skip']} skipped" if counts["skip"] else "")
                        + f" in {int((r['finished'] - r['started']) / 60)} min")
        self._say(r["summary"], "info" if state == "passed" else "error")
        self._flush()
        try:
            self._blobs.put(f"bench/{r['id']}.log", "\n".join(self._log_lines).encode("utf-8"))
        except Exception:
            log.exception("storing the bench log failed")
        self._docs.put(RUNS, r["id"], r)
        if self._audit:
            self._audit.record(r["user"], "bench.result", None, run=r["id"], tag=r["tag"], state=state)
        self._publish()

    def _step(self, sid: str, name: str, critical: bool, fn: Callable[[], str]) -> None:
        self._check_cancel()
        item = {"id": sid, "name": name, "result": "running", "detail": "", "started": time.time(), "finished": None}
        self._run["items"].append(item)
        self._publish()
        self._say(f"── {name}")
        try:
            item["detail"] = fn() or ""
            item["result"] = "pass"
        except _Fail as exc:
            item["result"], item["detail"] = "fail", str(exc)
        except Abort as exc:
            item["result"], item["detail"] = ("skip" if self._cancel.is_set() else "fail"), str(exc)
            item["finished"] = time.time()
            self._publish()
            raise
        item["finished"] = time.time()
        self._say(f"   {item['result'].upper()}: {item['detail']}", "info" if item["result"] == "pass" else "error")
        self._publish()
        if critical and item["result"] == "fail":
            raise Abort(f"{name} failed — later tests need it")

    # ── steps ─────────────────────────────────────────────────────────────────

    def _identity(self, dev: Device, spec: dict) -> str:
        notes = []
        mac = self._devinfo(dev, KEY_MAC, tries=5)
        if mac is None:
            raise _Fail(f"{dev.label} does not answer over MQTT")
        want = _hexid(dev.mqtt_id)
        if _hexid(mac) != want:
            raise _Fail(f"the device reports MAC {mac}, the registry entry {dev.mqtt_id}")
        notes.append(f"MQTT ok, MAC {mac}")
        # the UART must show this device's traffic (its MQTT topic carries the MAC)
        found = False
        for _ in range(3):
            m = self.uart.mark()
            self._devinfo(dev, KEY_FW, tries=1)
            self._sleep(3)
            lines = self.uart.since(m)
            if any(want in _hexid_line(l) for l in lines):
                found = True
                break
        if not found:
            got = len(self.uart.since(0))
            raise _Fail(f"{spec['device_port']} shows {'no output' if not got else 'another device'} "
                        f"— the selected device's MAC does not appear in its log")
        notes.append(f"UART {spec['device_port']} is this device")
        ip = (spec.get("device_ip") or "").strip()
        if ip:      # informational: the bench PC may be on another network than the device's WiFi
            if _ping(ip):
                notes.append(f"IP {ip} answers a ping")
            else:
                notes.append(f"IP {ip} does not answer a ping from this PC (other network?) — not a failure")
                self._say(f"Local IP {ip} does not answer a ping from this PC — the device may be on another network", "warn")
        return "; ".join(notes)

    def _firmware(self, dev: Device, main: dict, dt: Optional[dict]) -> str:
        hw = self._devinfo(dev, KEY_HW) or ""
        rules = {k.strip().lower(): v for k, v in self._cfg().ota.board_targets.items()}
        pats = [p.lower() for p in rules.get(hw.strip().lower(), [])]
        for b in (main, dt):
            if b and pats and not bundle_fits_board(b, pats):
                raise _Fail(f"{b['name']} v{b['version']} does not fit board {hw} (Settings → OTA board rules)")
        fw, dtv = self._devinfo(dev, KEY_FW) or "", self._devinfo(dev, KEY_DT) or ""
        self._say(f"Board {hw}, device runs main {fw}, DT {dtv}")
        changed = []
        if fw != main["version"]:
            self._ota_flash(dev, main)
            changed.append(f"main {fw} → {main['version']}")
        if dt and dtv != dt["version"]:
            self._ota_flash(dev, dt)                   # stored on the SD card, flashed by main at its next boot
            changed.append(f"DT {dtv} → {dt['version']}")
        if changed:
            # Main flashes a new DT image from its SD card at the next boot (the ESP32 DT download takes
            # up to ~1.5 min): wait that long after each reboot. At least 2, at most 3 reboots.
            dt_new = bool(dt) and any(c.startswith("DT") for c in changed)
            wait_s = 90 if dt_new else 10
            for n in range(1, 4):
                self._reboot(dev)
                self._say(f"Waiting {wait_s} s" + (" for main to download the DT image" if dt_new else "") + f" (reboot {n})")
                self._sleep(wait_s)
                fw, dtv = self._devinfo(dev, KEY_FW) or "", self._devinfo(dev, KEY_DT) or ""
                done = fw == main["version"] and (not dt or dtv == dt["version"])
                if n >= 2 and done:
                    break
                if not done:
                    self._say(f"After reboot {n}: main {fw}, DT {dtv}", "warn")
            changed.append(f"{n} reboots")
        self._run["versions"] = {"main": fw, "dt": dtv, "board": hw}
        self._publish()
        if fw != main["version"]:
            raise _Fail(f"device runs main {fw}, expected {main['version']}")
        if dt and dtv != dt["version"]:
            raise _Fail(f"device runs DT {dtv}, expected {dt['version']}")
        return (f"board {hw}, main {fw}, DT {dtv}" + (f" (installed: {', '.join(changed)})" if changed else " (already installed)"))

    def _emu_mode(self, p: dict) -> None:
        self._emu(f"MODE {p['code']}", 3.5)
        if p["price"]:
            self._emu(f"PRICE {p['price']}")

    def _switch_type(self, dev: Device, p: dict) -> str:
        """Set the device's display type to p and verify it on main and DT: with the emulator playing p,
        frames of p's type must arrive. After a change: at least 2, at most 3 reboots."""
        cur = self._cfg_read(dev, KEY_DISPLAY_TYPE)
        if str(cur) == str(p["type"]):
            if self._wait_frames(p["type"], 30) is not None:
                return f"type {p['type']} already set"
            min_reboots = 1                            # set, but the DT does not run it: reboot until it does
        else:
            self._ops.write_config(dev, KEY_DISPLAY_TYPE, str(p["type"]), user="bench")
            self._sleep(4)
            min_reboots = 2                            # acceptance procedure: 2 reboots after a type switch
        for n in range(1, 4):
            self._reboot(dev)
            if n < min_reboots:
                continue
            if self._wait_frames(p["type"], 60) is not None:
                return f"type {cur} → {p['type']}, {n} reboot(s)"
            self._say(f"No frames of type {p['type']} after reboot {n}", "warn")
        raise _Fail(f"display type {p['type']} not running after 3 reboots (no frames of the type)")

    def _pump_type(self, dev: Device, p: dict, sales: int, pump_s: float) -> str:
        self._emu_mode(p)
        notes = [self._switch_type(dev, p)]
        results = []
        for n in range(1, sales + 1):
            ok, detail = self._sale(p, pump_s)
            results.append(ok)
            self._say(f"   sale {n}: {'PASS' if ok else 'FAIL'} — {detail}", "info" if ok else "error")
            notes.append(f"sale {n} {'ok' if ok else 'FAIL'}: {detail}")
            self._sleep(3)
        if not all(results):
            raise _Fail("; ".join(notes))
        return "; ".join(notes)

    def _mismatch(self, dev: Device, d: dict, e: dict, secs: float) -> str:
        """Device set to display type d, the emulator plays another type e (a wrong display on the bus).
        The device must keep answering MQTT, must not flood its log, crash or reset."""
        cur = self._cfg_read(dev, KEY_DISPLAY_TYPE)
        setup = ""
        if str(cur) != str(d["type"]):
            self._emu_mode(d)                          # verify the device type with its own display first
            setup = self._switch_type(dev, d) + "; "
        self._emu_mode(e)
        m, t0 = self.uart.mark(), time.time()
        lat: list[float] = []
        misses = 0

        def probe() -> None:
            nonlocal misses
            t = time.time()
            if self._devinfo(dev, KEY_FW, tries=1, timeout=5) is None:
                misses += 1
            else:
                lat.append(time.time() - t)

        self._emu("NOZZLE 1", 2)
        self._emu("PUMP 1", 0.5)
        end = time.time() + secs
        while time.time() < end:                       # requests while the wrong display sends data
            probe()
            self._sleep(max(0.0, min(3.0, end - time.time())))
        self._emu("PUMP 0", 1)
        self._emu("NOZZLE 0", 3)
        for _ in range(3):                             # and after it stops
            probe()
            self._sleep(2)
        self._emu("NOZZLE AUTO", 0.3)
        self._emu("PUMP AUTO", 0.3)

        lines = self.uart.since_ts(m)
        dur = max(1.0, time.time() - t0)
        per_s: dict[int, int] = {}
        for ts, _ in lines:
            per_s[int(ts)] = per_s.get(int(ts), 0) + 1
        rate, peak = len(lines) / dur, max(per_s.values(), default=0)
        texts = [t for _, t in lines]
        crashes = [t for t in texts if any(c in t for c in _CRASH)]
        resets = [x.group(2) for t in texts for x in [_RE_RESET.search(t)] if x]
        wd = sum(1 for t in texts if "DT watchdog" in t)
        sales = [dd for ts, msg, dd in self._events if msg == "MsgFuelPumped" and ts >= t0]

        problems = []
        n = misses + len(lat)
        if misses:
            problems.append(f"MQTT: {misses} of {n} requests unanswered")
        if rate > MISMATCH_MAX_LINES_S or peak > MISMATCH_MAX_PEAK_S:
            problems.append(f"log flood: {rate:.1f} lines/s average, {peak} in the busiest second")
        if crashes:
            problems.append(f"crash: {crashes[0][:120]}")
        if resets:
            problems.append(f"device reset during the test: {', '.join(resets)}")
        info = (f"{setup}MQTT {len(lat)}/{n} answered (max {max(lat, default=0):.1f} s, "
                f"avg {sum(lat) / max(1, len(lat)):.1f} s); log {len(lines)} lines in {dur:.0f} s "
                f"({rate:.1f}/s, peak {peak}/s); DT watchdog {wd}×"
                + (f"; NOTE {len(sales)} sale(s) reported from the wrong display" if sales else ""))
        if problems:
            raise _Fail("; ".join(problems) + f" — {info}")
        return info

    def _pumping_status(self, dev: Device) -> str:
        cur = str(self._cfg_read(dev, KEY_DISPLAY_TYPE))
        p = next((x for x in PUMP_TYPES if str(x["type"]) == cur), None)
        if not p:
            raise _Fail(f"display type {cur} has no emulator mode")
        self._emu(f"MODE {p['code']}", 3.5)
        if p["price"]:
            self._emu(f"PRICE {p['price']}")
        self._ops.write_config(dev, KEY_PUMP_STATUS_EN, "true", user="bench")
        self._ops.write_config(dev, KEY_PUMP_STATUS_INT, "5", user="bench")
        self._sleep(2)
        t0 = time.time()
        ok, detail = self._sale(p, 14)
        status = [(d.get("nozzle_idx"), d.get("vol_lx1000", 0)) for ts, m, d in self._events
                  if m == "MsgPumpingStatus" and ts >= t0]
        per = {nz: [v for n, v in status if n == nz] for nz in (0, 1)}
        problems = []
        for nz, vols in per.items():
            if len(vols) < 2:
                problems.append(f"nozzle {nz}: {len(vols)} status message(s), expected ≥ 2 in 14 s at 5 s")
            elif vols != sorted(vols):
                problems.append(f"nozzle {nz}: volume not rising {vols}")
        if not ok:
            problems.append(f"sale: {detail}")
        summary = ", ".join(f"n{nz}: {len(v)}× {[x / 1000 for x in v]} L" for nz, v in per.items())
        if problems:
            raise _Fail("; ".join(problems) + f" ({summary})")
        return summary

    def _ota_roundtrip(self, dev: Device, main: dict, alt: dict) -> str:
        notes = []
        for b in (alt, main):
            self._ota_flash(dev, b)
            self._sleep(5)
            if not self._wait_online(dev, 120):
                raise _Fail(f"device did not come back after the OTA to {b['version']}")
            fw = self._devinfo(dev, KEY_FW) or ""
            if fw != b["version"]:
                raise _Fail(f"after the OTA to {b['version']} the device reports {fw}")
            notes.append(f"{b['version']} ok")
        return " → ".join(notes)

    def _uart_health(self) -> str:
        lines = self.uart.since(0)
        crashes = [l for l in lines if any(c in l for c in _CRASH)]
        resets = [m.group(2) for l in lines for m in [_RE_RESET.search(l)] if m]
        bad = [r for r in resets if r not in _OK_RESETS]
        wd = sum(1 for l in lines if "DT watchdog" in l)
        info = f"{len(lines)} lines, {len(resets)} resets ({', '.join(sorted(set(resets))) or 'none'}), DT watchdog {wd}×"
        if crashes or bad:
            raise _Fail((f"crash lines: {crashes[:3]}; " if crashes else "")
                        + (f"unexpected resets: {bad}; " if bad else "") + info)
        return info

    # ── building blocks ───────────────────────────────────────────────────────

    def _sale(self, p: dict, pump_s: float) -> tuple[bool, str]:
        """One sale on both nozzles; checks both MsgFuelPumped events against the emulator."""
        t0 = time.time()
        if p.get("price"):
            self._emu(f"PRICE {p['price']}")
        self._emu("NOZZLE 1", 3)
        self._emu("PUMP 1", pump_s)
        self._emu("PUMP 0", 2)
        st = self._emu("STATUS", 0.5)
        self._emu("NOZZLE 0", 10)
        self._emu("NOZZLE AUTO", 0.3)
        self._emu("PUMP AUTO", 0.3)
        self._sleep(2)
        m = _RE_EMU.search(st)
        vol_ml, price, total = (int(x) for x in m.groups()) if m else (0, 0, 0)
        sales = {}
        for ts, msg, d in self._events:
            if msg == "MsgFuelPumped" and ts >= t0:
                sales[int(d.get("nozzle_idx", -1))] = (int(d.get("vol_lx1000", 0)), int(d.get("unit_pricex100", 0)),
                                                       int(d.get("total_pricex100", 0)))
        problems = []
        if not m:
            problems.append("emulator STATUS not read")
        for nz in (0, 1):
            if nz not in sales:
                problems.append(f"n{nz}: no sale")
                continue
            v, u, t = sales[nz]
            if abs(v - vol_ml) > 10:
                problems.append(f"n{nz}: vol {v} ≠ emulator {vol_ml}")
            if abs(t - v * u / 1000) > 10:
                problems.append(f"n{nz}: total {t} ≠ vol × unit {v * u / 1000:.0f}")
            if p.get("price") and abs(u - int(p["price"])) > 10:
                problems.append(f"n{nz}: unit {u} ≠ {p['price']}")
        got = " ".join(f"n{k}={v[0] / 1000:.3f}L×{v[1] / 100:.2f}={v[2] / 100:.2f}" for k, v in sorted(sales.items()))
        detail = f"emulator {vol_ml / 1000:.3f} L / {price / 100:.2f} / {total / 100:.2f}, device [{got}]"
        return (not problems), detail + (f" — {'; '.join(problems)}" if problems else "")

    def _ota_flash(self, dev: Device, b: dict) -> None:
        self._say(f"OTA {b['name']} v{b['version']} …")
        self._ota.start(dev, b["id"], None, user="bench")
        deadline = time.time() + 1200
        while not self._ota.wait(dev.id, 2):
            self._check_cancel()
            if time.time() > deadline:
                self._ota.abort(dev.id, "bench", dev)
                raise _Fail(f"OTA of {b['version']} did not finish within 20 min")
        s = self._ota.session(dev.id) or {}
        if s.get("state") != "succeeded":
            raise _Fail(f"OTA of {b['name']} v{b['version']} {s.get('state', 'failed')}")
        self._say(f"OTA {b['name']} v{b['version']} succeeded")

    def _reboot(self, dev: Device) -> None:
        m = self.uart.mark()
        self._ops.send(dev, "MsgSystemReboot", {})
        self._say("Reboot")
        end = time.time() + 20
        while time.time() < end and not any("rst:0x" in l for l in self.uart.since(m)):
            self._sleep(0.5)
        self._sleep(6)
        if not self._wait_online(dev, 90):
            raise _Fail("device did not come back online within 90 s after a reboot")
        ips = [x.group(1) for l in self.uart.since(m) for x in [_RE_IP.search(l)] if x]
        want = (self._run["spec"].get("device_ip") or "").strip()
        if ips and want and ips[-1] != want:
            self._say(f"Note: the device got IP {ips[-1]}, the run was set up with {want}", "warn")

    def _wait_online(self, dev: Device, timeout: float) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            if self._devinfo(dev, KEY_FW, tries=1, timeout=3) is not None:
                return True
            self._sleep(2)
        return False

    def _wait_frames(self, dtype: int, timeout: float) -> Optional[int]:
        """Wait until the UART shows display frames carrying `dtype` (the DT's real running type)."""
        m = self.uart.mark()
        end = time.time() + timeout
        while time.time() < end:
            for l in self.uart.since(m):
                # either nozzle: an idle bus may only update one display (Wayne-2 in the emulator)
                if "Received frame:" in l and any(int(t) == dtype for t in _RE_FRAME.findall(l)):
                    return dtype
            self._sleep(1)
        return None

    def _emu(self, cmd: str, wait: float = 0.4, cancellable: bool = True) -> str:
        if not self.emu:
            return ""
        m = self.emu.mark()
        self.emu.write(cmd + "\n")
        if cancellable:
            self._sleep(wait)
        else:
            time.sleep(wait)
        return "\n".join(self.emu.since(m))

    def _devinfo(self, dev: Device, key: int, tries: int = 3, timeout: float = 5) -> Optional[str]:
        for _ in range(tries):
            try:
                r = self._ops.read_devinfo(dev, key, timeout=timeout)
                return r["value"] if r["valid"] else ""
            except Exception:
                self._sleep(2)
        return None

    def _cfg_read(self, dev: Device, key: int):
        for _ in range(3):
            try:
                return self._ops.read_config(dev, key)["value"]
            except Exception:
                self._sleep(2)
        raise _Fail(f"config 0x{key:04X} could not be read")

    def _sleep(self, s: float) -> None:
        if self._cancel.wait(s):
            raise Abort("cancelled by the user")

    def _check_cancel(self) -> None:
        if self._cancel.is_set():
            raise Abort("cancelled by the user")

    # ── events / log ──────────────────────────────────────────────────────────

    def _on_mqtt(self, topic_id: str, kind: str, payload: dict, group: str = "") -> None:
        if kind != "evt" or topic_id != self._topic or not self.current():
            return
        msg, data = payload.get("msg", ""), payload.get("data") or {}
        self._events.append((time.time(), msg, data))
        if msg in ("MsgFuelPumped", "MsgPumpingStatus", "MsgNozzleState"):
            self._line("mqtt", time.time(), f"{msg} {data}")

    def _on_serial(self, src: str, ts: float, text: str) -> None:
        self._line(src, ts, text)

    def _say(self, text: str, level: str = "info") -> None:
        self._line("runner" if level == "info" else level, time.time(), text)

    def _line(self, src: str, ts: float, text: str) -> None:
        stamp = datetime.fromtimestamp(ts).strftime("%H:%M:%S.%f")[:-3]
        self._log_lines.append(f"{stamp} [{src}] {text}")
        with self._pending_lock:
            self._pending.append({"ts": ts, "src": src, "text": text})

    def _flush_loop(self) -> None:
        while True:
            time.sleep(0.3)
            self._flush()

    def _flush(self) -> None:
        with self._pending_lock:
            lines, self._pending = self._pending, []
        if lines and self._run:
            self._bus.publish({"type": "bench.log", "run_id": self._run["id"], "lines": lines})

    def _publish(self) -> None:
        cur = self.current()
        if cur:
            self._bus.publish({"type": "bench.state", "run": cur})


class _Fail(Exception):
    """A step failed (reported; the run goes on unless the step is critical)."""


def _short(meta: Optional[dict]) -> Optional[dict]:
    return {k: meta.get(k) for k in ("id", "name", "version", "filename")} if meta else None


def _hexid(s: str) -> str:
    return re.sub(r"[^0-9a-f]", "", (s or "").lower())


def _hexid_line(line: str) -> str:
    return line.lower().replace(":", "")


def _ping(ip: str) -> bool:
    flag = "-n" if platform.system() == "Windows" else "-c"
    try:
        r = subprocess.run(["ping", flag, "2", ip], capture_output=True, timeout=10)
        return r.returncode == 0 and b"TTL=" in r.stdout.upper()
    except Exception:
        return False
