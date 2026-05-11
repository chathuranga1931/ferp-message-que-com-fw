"""
main.py — FERP Device Tool
===========================
Merged GUI combining the FERP MQTT Tool (message tree, OTA) and the
FERP Config Tool (config key read/write, device info, WebAPI/UART transport).

Layout
------
  ┌─ Connection ──────────────────────────────────────────────────────────────┐
  │  Interface▾ [MQTT|WebAPI|UART]  |  <interface-specific fields>  | Connect●│
  └───────────────────────────────────────────────────────────────────────────┘
  ┌─ Device Info ──────────────────────────────────────────────────────────────┐
  │  Device UUID: [__________↺]  Group: [______↺]  HW Address: [_______↺]↺All│
  └────────────────────────────────────────────────────────────────────────────┘

  ┌─── Left panel (PanedWindow vertical) ────┬──── Right panel ──────────────┐
  │  [🔍 filter]                              │  ┌─ OTA Upgrade ────────────┐ │
  │  ┌─ Message Tree ────────────────────┐   │  │ Browse… target ver chunk │ │
  │  │ ▸ Config (8)  ▸ OTA …  ▸ Btn … │   │  │ ████░░░░  42%            │ │
  │  └────────────────────────────────── ┘   │  │ [Start OTA] [Abort]     │ │
  │  ┌─ Command Input ─────────────────────┐  │  └─────────────────────────┘ │
  │  │  <fields>              [Send]       │  │  ┌─ Config Keys ────────────┐ │
  │  └──────────────────────────────────── ┘  │  │ #  KeyID  Name  Type Val│ │
  │                                            │  │ ⬇ Read All  ⬆ Write All│ │
  │                                            │  └─────────────────────────┘ │
  └────────────────────────────────────────────┴──────────────────────────────┘
  ┌─ Console ──────────────────────────────────────────────────────── [Clear] ┤
  └────────────────────────────────────────────────────────────────────────────┘

Config keys are loaded from config_keys.json — no code change needed for new keys.
Messages are loaded from src/app-messages/messages/**/*.json.
"""

import json
import os
import platform
import queue
import sys
import threading
import time
import tkinter as tk
from datetime import datetime
from pathlib import Path
from tkinter import ttk, scrolledtext, messagebox, simpledialog, filedialog
from typing import Dict, List, Optional, Tuple

# ── PyQt6 is used only for the Worker/signals pattern; GUI is tkinter ──────────
# This tool uses tkinter (same as original MQTT tool) to stay light-weight.

try:
    import paho.mqtt.client as _paho_check  # noqa: F401
except ImportError:
    print("ERROR: paho-mqtt not installed.\nRun:  pip install paho-mqtt", file=sys.stderr)
    sys.exit(1)

from transports import (
    MqttRawClient, MQTTTransport, WebAPITransport, UARTTransport,
    TransportError, encode_value, decode_value, type_id_from_name,
    TYPE_UINT32, TYPE_STRING, TYPE_BOOL, TYPE_NAMES,
    _build_topic_base,
)
from messages.msg_loader import MSG_DEFS
from mqtt_ota import MqttOtaHandler

# Conditional import — webapi OTA is a stub; don't fail if not yet complete
try:
    from webapi_ota import WebApiOtaHandler
    _WEBAPI_OTA_AVAILABLE = True
except Exception:
    _WEBAPI_OTA_AVAILABLE = False


# ─────────────────────────────────────────────────────────────────────────────
# Paths / settings
# ─────────────────────────────────────────────────────────────────────────────

_TOOL_DIR        = Path(__file__).parent
SETTINGS_FILE    = Path.home() / ".ferp-device-tool.json"
DEVICES_FILE     = _TOOL_DIR / "ferp_devices.json"
NETWORK_FILE     = _TOOL_DIR / "ferp_network_config.json"
CONFIG_KEYS_FILE = _TOOL_DIR / "config_keys.json"


def _load_json(path: Path, default):
    try:
        if path.exists():
            return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        pass
    return default


def _save_json(path: Path, data) -> None:
    try:
        path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    except Exception:
        pass


def _load_settings() -> dict:
    return _load_json(SETTINGS_FILE, {})


def _save_settings(data: dict) -> None:
    _save_json(SETTINGS_FILE, data)


def _load_devices() -> list:
    return _load_json(DEVICES_FILE, {}).get("devices", [])


def _load_ota_targets(iface: str) -> List[Tuple[str, str]]:
    """
    Return [(label, name), ...] for the given interface ("mqtt" or "webapi").
    Falls back to a minimal built-in list when the JSON key is missing.
    """
    key = iface.lower()
    entries = _load_json(DEVICES_FILE, {}).get("ota_targets", {}).get(key, [])
    result = [(e["label"], e["name"]) for e in entries if "label" in e and "name" in e]
    if not result:
        # built-in fallback so the combo is never empty
        if key == "mqtt":
            result = [("esp32-main", "esp32-main"), ("esp32-dt-boot", "esp32-dt-boot"),
                      ("esp32-dt-part", "esp32-dt-part"), ("esp32-dt-fw", "esp32-dt-fw")]
        else:
            result = [("main", "main"), ("dt-bootloader", "dt-bootloader"),
                      ("dt-partitions", "dt-partitions"), ("dt-app", "dt-app")]
    return result


def _load_network_config() -> dict:
    return _load_json(NETWORK_FILE, {})


def _net_mqtt(cfg: dict) -> dict:
    return cfg.get("mqtt", {})


def _net_webapi(cfg: dict) -> dict:
    return cfg.get("webapi", {})


# ─────────────────────────────────────────────────────────────────────────────
# Config keys — loaded from config_keys.json
# ─────────────────────────────────────────────────────────────────────────────

def _load_config_keys() -> List[Tuple[int, str, int, str]]:
    """
    Returns list of (key_id, name, type_id, group_label) tuples.
    Loaded from config_keys.json so no code changes are needed for new keys.
    """
    data = _load_json(CONFIG_KEYS_FILE, {"groups": []})
    result = []
    for grp in data.get("groups", []):
        grp_label = grp.get("group", "Other")
        for k in grp.get("keys", []):
            try:
                key_id  = int(k["key"], 16)
                name    = k["name"]
                type_id = type_id_from_name(k.get("type", "STRING"))
                result.append((key_id, name, type_id, grp_label))
            except Exception:
                pass
    return result


CONFIG_KEYS = _load_config_keys()


# ─────────────────────────────────────────────────────────────────────────────
# Device info keys — loaded from deviceinfo-keys.json
# ─────────────────────────────────────────────────────────────────────────────

def _load_devinfo_keys() -> List[Tuple[int, str, str]]:
    """
    Returns list of (key_id, bar_label, field) tuples for the Device Info bar.
    Derived from the options of the 'key' field in MsgDevInfoRead (MSG_DEFS),
    using the 'bar_label' and 'field' metadata added to each option entry.
    Falls back to hardcoded defaults if the message definition is missing.
    """
    opts = (MSG_DEFS.get("MsgDevInfoRead", {})
                    .get("fields", [{}])[0]
                    .get("options", []))
    result = []
    for o in opts:
        try:
            key_id = int(o["value"])
            label  = o.get("bar_label") or o.get("label", "")
            field  = o.get("field", "")
            result.append((key_id, label, field))
        except Exception:
            pass
    if not result:
        result = [
            (0xA001, "Device UUID",  "uuid"),
            (0xA002, "Device Group", "group"),
            (0xA003, "HW Address",   "mac"),
            (0xA004, "FW Version",   "fw_version"),
            (0xA005, "HW Version",   "hw_version"),
        ]
    return result


DEV_INFO_KEYS = _load_devinfo_keys()


# ─────────────────────────────────────────────────────────────────────────────
# Message tree grouping
# ─────────────────────────────────────────────────────────────────────────────

_MSG_GROUPS: dict[str, list[str]] = {}
for _n, _d in sorted(MSG_DEFS.items()):
    _g = _d.get("group", "Other")
    _MSG_GROUPS.setdefault(_g, []).append(_n)


# ─────────────────────────────────────────────────────────────────────────────
# Dialogs
# ─────────────────────────────────────────────────────────────────────────────

class _AddBrokerDialog(simpledialog.Dialog):
    def body(self, master):
        self.title("Add broker")
        tk.Label(master, text="IP / hostname:").grid(row=0, column=0, sticky="w")
        self.broker_var = tk.StringVar()
        tk.Entry(master, textvariable=self.broker_var, width=28).grid(row=0, column=1, padx=4)
        tk.Label(master, text="Port:").grid(row=1, column=0, sticky="w")
        self.port_var = tk.StringVar(value="1883")
        tk.Entry(master, textvariable=self.port_var, width=8).grid(row=1, column=1, sticky="w", padx=4)
        return None

    def apply(self):
        try:
            self.result = (self.broker_var.get().strip(), int(self.port_var.get().strip()))
        except ValueError:
            self.result = None


class _AddDeviceDialog(simpledialog.Dialog):
    def body(self, master):
        self.title("Add device")
        self.vars = []
        for i, (lbl, dflt) in enumerate([
            ("Label:", "My Device"),
            ("IP:", "192.168.4.1"),
            ("MAC:", "AA:BB:CC:DD:EE:FF"),
            ("UUID:", ""),
            ("Group:", "default"),
        ]):
            tk.Label(master, text=lbl).grid(row=i, column=0, sticky="w")
            v = tk.StringVar(value=dflt)
            tk.Entry(master, textvariable=v, width=30).grid(row=i, column=1, padx=4)
            self.vars.append(v)
        return None

    def apply(self):
        self.result = {
            "label": self.vars[0].get().strip(),
            "ip":    self.vars[1].get().strip(),
            "mac":   self.vars[2].get().strip(),
            "uuid":  self.vars[3].get().strip(),
            "group": self.vars[4].get().strip(),
        }


# ─────────────────────────────────────────────────────────────────────────────
# Message tree widget
# ─────────────────────────────────────────────────────────────────────────────

class MessageTree(ttk.Frame):
    def __init__(self, parent, on_select, **kw):
        super().__init__(parent, **kw)
        self._on_select = on_select
        self._filter_var = tk.StringVar()
        self._filter_var.trace_add("write", self._apply_filter)
        self._build_ui()

    def _build_ui(self):
        ff = ttk.Frame(self)
        ff.pack(fill="x", padx=4, pady=(4, 2))
        ttk.Label(ff, text="🔍").pack(side="left")
        ttk.Entry(ff, textvariable=self._filter_var).pack(side="left", fill="x", expand=True, padx=(4, 0))

        tv_frame = ttk.Frame(self)
        tv_frame.pack(fill="both", expand=True, padx=4, pady=(0, 4))

        self._tv = ttk.Treeview(tv_frame, columns=("dir",), show="tree headings", selectmode="browse")
        self._tv.heading("#0",  text="Message")
        self._tv.heading("dir", text="Dir")
        self._tv.column("#0",   width=180, stretch=True)
        self._tv.column("dir",  width=40,  stretch=False, anchor="center")

        scroll = ttk.Scrollbar(tv_frame, orient="vertical", command=self._tv.yview)
        self._tv.configure(yscrollcommand=scroll.set)
        self._tv.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")

        self._tv.tag_configure("cmd",   foreground="#0066cc")
        self._tv.tag_configure("resp",  foreground="#228b22")
        self._tv.tag_configure("any",   foreground="#7a5f00")
        self._tv.tag_configure("group", font=("TkDefaultFont", 10, "bold"))
        self._tv.bind("<<TreeviewSelect>>", self._on_tree_select)
        self._populate()

    def _populate(self, filter_text: str = ""):
        flt = filter_text.lower().strip()
        for item in self._tv.get_children():
            self._tv.delete(item)
        for group, names in sorted(_MSG_GROUPS.items()):
            matched = [n for n in names if flt in n.lower()] if flt else list(names)
            if not matched:
                continue
            gid = self._tv.insert("", "end", text=f"{group}  ({len(matched)})", open=True, tags=("group",))
            for name in sorted(matched):
                d   = MSG_DEFS[name]
                tag = d["direction"]
                lbl = name[3:] if name.startswith("Msg") else name
                self._tv.insert(gid, "end", iid=name, text=lbl, values=(d["direction"],), tags=(tag,))

    def _apply_filter(self, *_):
        self._populate(self._filter_var.get())

    def _on_tree_select(self, _=None):
        sel = self._tv.selection()
        if sel and sel[0] in MSG_DEFS:
            self._on_select(sel[0])


# ─────────────────────────────────────────────────────────────────────────────
# ─────────────────────────────────────────────────────────────────────────────
# WebApiSender — adapts WebAPITransport.send_message() to the same interface
# as MqttRawClient.send() so CommandPanel works with either transport.
# ─────────────────────────────────────────────────────────────────────────────

class WebApiSender:
    def __init__(self, transport: "WebAPITransport", log_fn):
        self._transport = transport
        self._log = log_fn

    def send(self, msg_name: str, data: dict) -> None:
        payload = {"msg": msg_name, "data": data}
        self._log(f"→ [cmd]  {json.dumps(payload)}", "cmd")
        try:
            resp = self._transport.send_message(msg_name, data)
            self._log(f"← [resp]  {json.dumps(resp, indent=2)}", "resp")
        except Exception as exc:
            self._log(f"[error] {exc}", "error")


# Command panel (below message tree)
# ─────────────────────────────────────────────────────────────────────────────

class CommandPanel(ttk.LabelFrame):
    def __init__(self, parent, get_sender_fn, **kw):
        super().__init__(parent, text="Command", **kw)
        self._get_client = get_sender_fn
        self._msg_name   = ""
        self._widgets: list = []
        self._build_ui()

    def _build_ui(self):
        self._info_lbl = ttk.Label(self, text="Select a message in the tree", foreground="#666666")
        self._info_lbl.pack(anchor="w", padx=6, pady=(4, 0))
        self._fields_frame = ttk.Frame(self)
        self._fields_frame.pack(fill="x", padx=6, pady=4)
        bar = ttk.Frame(self)
        bar.pack(fill="x", padx=6, pady=(0, 6))
        self._send_btn = ttk.Button(bar, text="Send", command=self._do_send, state="disabled")
        self._send_btn.pack(side="left")
        self._dir_lbl = ttk.Label(bar, text="", foreground="#888888")
        self._dir_lbl.pack(side="left", padx=8)
        self._note_lbl = ttk.Label(bar, text="", foreground="#aaaaaa")
        self._note_lbl.pack(side="left")

    def load_message(self, msg_name: str):
        self._msg_name = msg_name
        defn      = MSG_DEFS.get(msg_name, {})
        direction = defn.get("direction", "any")
        msg_id    = defn.get("msg_id", 0)
        fields    = defn.get("fields", [])

        self._info_lbl.config(text=f"{msg_name}   │   ID=0x{msg_id:04X}   │   {len(fields)} field(s)")
        sendable = direction in ("cmd", "any")
        dir_colors = {"cmd": "#0066cc", "resp": "#228b22", "any": "#7a5f00"}
        self._dir_lbl.config(text=f"[{direction}]", foreground=dir_colors.get(direction, "#444"))
        self._send_btn.config(state="normal" if sendable else "disabled")

        for w in self._fields_frame.winfo_children():
            w.destroy()
        self._widgets.clear()

        if not fields:
            ttk.Label(self._fields_frame, text="(no payload fields — empty {} sent)", foreground="gray"
                      ).grid(row=0, column=0, padx=4, sticky="w")
            return

        self._fields_frame.columnconfigure(1, weight=1)
        for row_idx, fdef in enumerate(fields):
            name    = fdef["name"]
            label   = fdef.get("label", name)
            ftype   = fdef.get("type", "string")
            default = fdef.get("default", "")
            options = fdef.get("options", [])
            ttk.Label(self._fields_frame, text=f"{label}:").grid(
                row=row_idx, column=0, sticky="e", padx=(4, 4), pady=2)
            if ftype == "enum" and options:
                labels = [str(o["label"]) for o in options]
                var    = tk.StringVar(value=labels[0] if labels else "")
                for opt in options:
                    if str(opt.get("value", "")) == str(default):
                        var.set(str(opt["label"]))
                        break
                ttk.Combobox(self._fields_frame, textvariable=var, values=labels, width=22, state="readonly"
                             ).grid(row=row_idx, column=1, sticky="w", padx=(0, 6), pady=2)
            elif ftype == "bool":
                var = tk.StringVar(value="true" if default else "false")
                ttk.Combobox(self._fields_frame, textvariable=var, values=["true", "false"], width=10, state="readonly"
                             ).grid(row=row_idx, column=1, sticky="w", padx=(0, 6), pady=2)
            else:
                var = tk.StringVar(value=str(default) if default != "" else "")
                e = ttk.Entry(self._fields_frame, textvariable=var, width=30)
                e.grid(row=row_idx, column=1, sticky="ew", padx=(0, 6), pady=2)
                # Show placeholder hint in the label if provided
                placeholder = fdef.get("placeholder", "")
                if placeholder and not var.get():
                    e.insert(0, placeholder)
                    e.config(foreground="gray")
                    def _on_focus_in(event, entry=e, sv=var, ph=placeholder):
                        if entry.get() == ph:
                            entry.delete(0, "end")
                            entry.config(foreground="")
                    def _on_focus_out(event, entry=e, sv=var, ph=placeholder):
                        if not entry.get().strip():
                            entry.insert(0, ph)
                            entry.config(foreground="gray")
                    e.bind("<FocusIn>",  _on_focus_in)
                    e.bind("<FocusOut>", _on_focus_out)
            self._widgets.append((name, var, fdef))

    def _do_send(self):
        if not self._msg_name:
            return
        client = self._get_client()
        if client is None:
            messagebox.showwarning("Not connected",
                                   "Connect to a device first to send messages.")
            return
        data: dict = {}
        for name, var, fdef in self._widgets:
            raw       = var.get().strip()
            ftype     = fdef.get("type", "string")
            # Ignore placeholder text (shown in gray when field is empty)
            placeholder = fdef.get("placeholder", "")
            if raw == placeholder:
                raw = ""
            if ftype == "enum":
                options = fdef.get("options", [])
                matched = next((o["value"] for o in options if str(o["label"]) == raw), None)
                data[name] = matched if matched is not None else raw
            elif ftype == "bool":
                data[name] = raw.lower() in ("1", "true", "yes")
            elif ftype in ("uint8", "uint16", "uint32", "int8", "int16", "int32"):
                try:
                    data[name] = int(raw, 0) if raw else 0
                except ValueError:
                    messagebox.showerror("Bad value", f"Field '{name}' expects integer, got: {raw!r}")
                    return
            elif ftype in ("float", "double"):
                try:
                    data[name] = float(raw) if raw else 0.0
                except ValueError:
                    messagebox.showerror("Bad value", f"Field '{name}' expects number, got: {raw!r}")
                    return
            else:
                data[name] = raw
        client.send(self._msg_name, data)

    def _get_sender(self):
        return self._get_client()


# ─────────────────────────────────────────────────────────────────────────────
# OTA panel (right side top)
# ─────────────────────────────────────────────────────────────────────────────

class OtaPanel(ttk.LabelFrame):
    def __init__(self, parent, get_ota_params, append_log, get_ota_targets, **kw):
        kw.setdefault("text", "OTA Upgrade")
        super().__init__(parent, **kw)
        self._get_params   = get_ota_params
        self._append_log   = append_log
        self._get_targets  = get_ota_targets  # () -> [(label, name), ...]
        self._handler      = None
        self._fw_path      = tk.StringVar()
        self._target_var   = tk.StringVar()
        self._ver_var      = tk.StringVar(value="1.0.0")
        self._chunk_var    = tk.StringVar(value="4096")
        # Internal map: display label -> wire name
        self._target_name_map: Dict[str, str] = {}
        self._build_ui()

    def _build_ui(self):
        fw = ttk.Frame(self)
        fw.pack(fill="x", padx=6, pady=(6, 2))
        ttk.Button(fw, text="Browse…", command=self._browse).pack(side="left")
        ttk.Entry(fw, textvariable=self._fw_path, state="readonly").pack(
            side="left", fill="x", expand=True, padx=(6, 0))

        opt = ttk.Frame(self)
        opt.pack(fill="x", padx=6, pady=2)
        ttk.Label(opt, text="Target:").pack(side="left")
        self._target_combo = ttk.Combobox(opt, textvariable=self._target_var,
                                           width=22, state="readonly")
        self._target_combo.pack(side="left", padx=(4, 12))
        ttk.Label(opt, text="Version:").pack(side="left")
        ttk.Entry(opt, textvariable=self._ver_var, width=10).pack(side="left", padx=(4, 12))
        ttk.Label(opt, text="Chunk:").pack(side="left")
        ttk.Combobox(opt, textvariable=self._chunk_var, values=["1024", "2048", "4096", "8192"],
                     width=7).pack(side="left", padx=(4, 0))

        pf = ttk.Frame(self)
        pf.pack(fill="x", padx=6, pady=2)
        self._progress = ttk.Progressbar(pf, mode="determinate")
        self._progress.pack(side="left", fill="x", expand=True)
        self._pct_lbl = ttk.Label(pf, text="  0%", width=6)
        self._pct_lbl.pack(side="left")

        bf = ttk.Frame(self)
        bf.pack(fill="x", padx=6, pady=2)
        self._start_btn = ttk.Button(bf, text="Start OTA", command=self._start)
        self._start_btn.pack(side="left", padx=(0, 8))
        self._abort_btn = ttk.Button(bf, text="Abort", command=self._abort, state="disabled")
        self._abort_btn.pack(side="left")

        lf = ttk.LabelFrame(self, text="OTA Log")
        lf.pack(fill="both", expand=True, padx=6, pady=(4, 6))
        self._log = scrolledtext.ScrolledText(lf, height=6, wrap="word", state="disabled",
                                              font=("Courier", 10))
        self._log.pack(fill="both", expand=True, padx=4, pady=4)
        self._log.tag_config("error", foreground="#cc0000")
        self._log.tag_config("warn",  foreground="#cc6600")
        self._log.tag_config("ok",    foreground="#228b22")
        ttk.Button(lf, text="Clear", command=self._clear_log).pack(
            side="right", padx=4, pady=(0, 4))

        # Populate with whatever interface is currently selected
        self.refresh_targets()

    def refresh_targets(self):
        """Reload the target combobox from the current interface's table."""
        entries = self._get_targets()  # [(label, name), ...]
        self._target_name_map = {label: name for label, name in entries}
        labels = [label for label, _ in entries]
        self._target_combo["values"] = labels
        if labels:
            # Keep selection if the same label still exists, else pick first
            if self._target_var.get() not in labels:
                self._target_var.set(labels[0])
        else:
            self._target_var.set("")

    def _resolved_target_name(self) -> str:
        """Return the wire-protocol name for the currently selected label."""
        label = self._target_var.get()
        return self._target_name_map.get(label, label)  # fallback: use label as-is

    def _browse(self):
        path = filedialog.askopenfilename(
            title="Select firmware .bin",
            filetypes=[("Binary firmware", "*.bin"), ("All files", "*.*")])
        if path:
            self._fw_path.set(path)

    def _start(self):
        params = self._get_params()
        if params is None:
            messagebox.showerror("Not configured", "Connect to a device first.")
            return
        fw = self._fw_path.get().strip()
        if not fw:
            messagebox.showerror("No firmware", "Select a firmware .bin file first.")
            return
        try:
            chunk = int(self._chunk_var.get())
        except ValueError:
            chunk = 4096

        iface, conn = params   # iface in ("mqtt", "webapi", "uart")

        try:
            if iface == "mqtt":
                self._handler = MqttOtaHandler(
                    broker=conn["broker"], port=conn["port"],
                    dev_type=conn["dev_type"], group=conn["group"],
                    device_id=conn["device_id"],
                    firmware_path=fw, target=self._resolved_target_name(),
                    version=self._ver_var.get().strip() or "unknown",
                    chunk_size=chunk,
                    on_log=self._cb_log,
                    on_progress=self._cb_progress,
                    on_done=self._cb_done,
                )
            elif iface == "webapi":
                if not _WEBAPI_OTA_AVAILABLE:
                    messagebox.showwarning("Not available", "WebAPI OTA is not yet available.")
                    return
                self._handler = WebApiOtaHandler(
                    ip=conn["ip"], port=conn["port"],
                    firmware_path=fw, target=self._resolved_target_name(),
                    version=self._ver_var.get().strip() or "unknown",
                    chunk_size=chunk,
                    on_log=self._cb_log,
                    on_progress=self._cb_progress,
                    on_done=self._cb_done,
                )
            else:
                messagebox.showwarning("Not supported", "OTA is not supported over UART.")
                return
        except Exception as e:
            self._log_ota(f"[error] {e}", "error")
            return

        self._log_ota(f"Starting OTA — interface={iface}", "ok")
        self._progress["value"] = 0
        self._pct_lbl.config(text="  0%")
        self._start_btn.config(state="disabled")
        self._abort_btn.config(state="normal")
        self._handler.start()

    def _abort(self):
        if self._handler:
            self._handler.abort()
        self._abort_btn.config(state="disabled")

    def _cb_log(self, msg: str):
        tag = "error" if "[error]" in msg else ("warn" if "[warn]" in msg else "")
        self.after(0, self._log_ota, msg, tag)
        self.after(0, self._append_log, f"[OTA] {msg}", "info")

    def _cb_progress(self, pct: int):
        self.after(0, self._set_progress, pct)

    def _cb_done(self, ok: bool):
        self.after(0, self._finished, ok)

    def _set_progress(self, pct: int):
        self._progress["value"] = pct
        self._pct_lbl.config(text=f"{pct:3d}%")

    def _finished(self, ok: bool):
        self._start_btn.config(state="normal")
        self._abort_btn.config(state="disabled")
        self._handler = None
        if ok:
            self._log_ota("OTA succeeded!", "ok")
            messagebox.showinfo("OTA Complete", "Firmware update complete. Device rebooting.")
        else:
            self._log_ota("OTA failed or aborted.", "error")

    def _log_ota(self, text: str, tag: str = ""):
        ts = time.strftime("%H:%M:%S")
        self._log.config(state="normal")
        self._log.insert("end", f"[{ts}] {text}\n", tag or "")
        self._log.see("end")
        self._log.config(state="disabled")

    def _clear_log(self):
        self._log.config(state="normal")
        self._log.delete("1.0", "end")
        self._log.config(state="disabled")


# ─────────────────────────────────────────────────────────────────────────────
# Config Keys panel (right side bottom)
# ─────────────────────────────────────────────────────────────────────────────

# Table column constants
_C_IDX  = 0
_C_KEY  = 1
_C_GRP  = 2
_C_NAME = 3
_C_TYPE = 4
_C_VAL  = 5

_CLR_UNLOADED = "#888888"
_CLR_LOADED   = "#228b22"
_CLR_MODIFIED = "#cc6600"


class ConfigKeysPanel(ttk.LabelFrame):
    """Displays config keys table with per-row read/write buttons."""

    def __init__(self, parent, on_read_key, on_write_key, on_read_all, on_write_all, **kw):
        kw.setdefault("text", "Config Keys")
        super().__init__(parent, **kw)
        self._on_read_key  = on_read_key
        self._on_write_key = on_write_key
        self._on_read_all  = on_read_all
        self._on_write_all = on_write_all
        self._key_row: Dict[int, int] = {}
        self._row_key: Dict[int, int] = {}
        self._connected    = False
        self._build_ui()

    def _build_ui(self):
        # Toolbar
        bar = ttk.Frame(self)
        bar.pack(fill="x", padx=4, pady=(4, 2))
        self._read_all_btn = ttk.Button(bar, text="⬇ Read All",  command=self._on_read_all,  state="disabled")
        self._read_all_btn.pack(side="left", padx=(0, 4))
        self._write_all_btn = ttk.Button(bar, text="⬆ Write All", command=self._on_write_all, state="disabled")
        self._write_all_btn.pack(side="left")
        self._progress = ttk.Progressbar(bar, mode="determinate", length=160)
        self._progress.pack(side="right", padx=4)
        self._progress.pack_forget()  # hidden by default

        # Table
        cols = ("idx", "key", "group", "name", "type", "value", "r", "w")
        self._tree = ttk.Treeview(self, columns=cols, show="headings", selectmode="browse")
        for col, label, width, anchor in [
            ("idx",   "#",      30,  "center"),
            ("key",   "Key ID", 70,  "center"),
            ("group", "Group",  75,  "w"),
            ("name",  "Name",   160, "w"),
            ("type",  "Type",   60,  "center"),
            ("value", "Value",  200, "w"),
            ("r",     "↺",      30,  "center"),
            ("w",     "✎",      30,  "center"),
        ]:
            self._tree.heading(col, text=label)
            self._tree.column(col, width=width, anchor=anchor, stretch=(col == "value"))

        vsb = ttk.Scrollbar(self, orient="vertical",   command=self._tree.yview)
        hsb = ttk.Scrollbar(self, orient="horizontal", command=self._tree.xview)
        self._tree.configure(yscrollcommand=vsb.set, xscrollcommand=hsb.set)
        self._tree.pack(side="left", fill="both", expand=True, padx=(4, 0), pady=(0, 4))
        vsb.pack(side="right", fill="y", pady=(0, 4))

        self._tree.tag_configure("unloaded", foreground=_CLR_UNLOADED)
        self._tree.tag_configure("loaded",   foreground=_CLR_LOADED)
        self._tree.tag_configure("modified", foreground=_CLR_MODIFIED)
        self._tree.bind("<Double-1>",       self._on_double_click)
        self._tree.bind("<ButtonRelease-1>", self._on_cell_click)

        self._populate()

    def _populate(self):
        for item in self._tree.get_children():
            self._tree.delete(item)
        self._key_row.clear()
        self._row_key.clear()
        for row_idx, (key_id, name, type_id, grp) in enumerate(CONFIG_KEYS):
            iid = self._tree.insert("", "end", tags=("unloaded",), values=(
                row_idx + 1,
                f"0x{key_id:04X}",
                grp,
                name,
                TYPE_NAMES.get(type_id, str(type_id)),
                "",
                "↺",
                "✎",
            ))
            self._key_row[key_id] = iid
            self._row_key[iid]    = key_id

    def set_connected(self, connected: bool):
        self._connected = connected
        state = "normal" if connected else "disabled"
        self._read_all_btn.config(state=state)
        self._write_all_btn.config(state=state)

    def _on_cell_click(self, event):
        """Handle clicks on the ↺ (read) and ✎ (write) icon columns."""
        if not self._connected:
            return
        col = self._tree.identify_column(event.x)   # "#7" or "#8" (1-based)
        iid = self._tree.identify_row(event.y)
        if not iid:
            return
        key_id = self._row_key.get(iid)
        if key_id is None:
            return
        if col == "#7":    # ↺ read
            self._on_read_key(key_id)
        elif col == "#8":  # ✎ write
            self._on_write_key(key_id)

    def update_key(self, key_id: int, type_id: int, data: list):
        iid = self._key_row.get(key_id)
        if iid is None:
            return
        value_str = decode_value(data, type_id)
        vals = list(self._tree.item(iid, "values"))
        vals[_C_TYPE] = TYPE_NAMES.get(type_id, str(type_id))
        vals[_C_VAL]  = value_str
        self._tree.item(iid, values=vals, tags=("loaded",))

    def mark_written(self, key_id: int):
        iid = self._key_row.get(key_id)
        if iid is not None:
            self._tree.item(iid, tags=("loaded",))

    def show_progress(self, current: int, total: int):
        if total == 0:
            return
        if current >= total:
            self._progress.pack_forget()
        else:
            self._progress.config(maximum=total, value=current)
            if not self._progress.winfo_ismapped():
                self._progress.pack(side="right", padx=4)

    def _on_double_click(self, event):
        """Allow editing the Value column by double-clicking."""
        region = self._tree.identify("region", event.x, event.y)
        col    = self._tree.identify_column(event.x)
        if region != "cell" or col != "#6":  # column 6 = value (1-indexed)
            return
        iid = self._tree.identify_row(event.y)
        if not iid:
            return
        key_id = self._row_key.get(iid)
        if key_id is None:
            return
        vals = list(self._tree.item(iid, "values"))
        cur_val = vals[_C_VAL]
        new_val = simpledialog.askstring("Edit Value",
                                         f"New value for {vals[_C_NAME]}:",
                                         initialvalue=cur_val,
                                         parent=self)
        if new_val is not None:
            vals[_C_VAL] = new_val
            self._tree.item(iid, values=vals, tags=("modified",))

    def get_all_values(self) -> List[Tuple[int, int, str]]:
        """Return [(key_id, type_id, value_str)] for all keys."""
        result = []
        for key_id, name, type_id, grp in CONFIG_KEYS:
            iid = self._key_row.get(key_id)
            if iid:
                vals = self._tree.item(iid, "values")
                result.append((key_id, type_id, vals[_C_VAL] if len(vals) > _C_VAL else ""))
        return result


# ─────────────────────────────────────────────────────────────────────────────
# Background worker thread
# ─────────────────────────────────────────────────────────────────────────────

class _WorkerEvent:
    """Simple thread-safe event queue callbacks."""
    def __init__(self):
        self._q: queue.Queue = queue.Queue()

    def put(self, fn):
        self._q.put(fn)

    def flush(self, root: tk.Tk):
        """Call from main thread to drain callbacks."""
        while not self._q.empty():
            try:
                fn = self._q.get_nowait()
                fn()
            except Exception:
                pass


class ConfigWorker:
    """
    Runs config read/write operations in a background thread.
    Results are posted as callables to a queue; the GUI polls via after().
    """

    def __init__(self, callback_queue: _WorkerEvent):
        self._q  = queue.Queue()
        self._cb = callback_queue
        self._transport = None
        self._running   = True
        self._thread    = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def enqueue(self, task: tuple):
        self._q.put(task)

    def stop(self):
        self._running = False
        self._q.put(None)

    # ── callbacks helpers ──────────────────────────────────────────────────────

    def _emit_log(self, msg: str, level: str = "info"):
        self._cb.put(lambda m=msg, l=level: _GUI_INSTANCE._append_log(m, l))

    def _emit_key_read(self, key: int, type_id: int, data: list):
        self._cb.put(lambda k=key, t=type_id, d=data: _GUI_INSTANCE._on_key_read(k, t, d))

    def _emit_write_done(self, key: int):
        self._cb.put(lambda k=key: _GUI_INSTANCE._on_write_done(k))

    def _emit_connected(self, ok: bool, msg: str):
        self._cb.put(lambda o=ok, m=msg: _GUI_INSTANCE._on_connected(o, m))

    def _emit_progress(self, cur: int, total: int):
        self._cb.put(lambda c=cur, t=total: _GUI_INSTANCE._on_progress(c, t))

    def _emit_batch_done(self):
        self._cb.put(lambda: _GUI_INSTANCE._on_batch_done())

    def _emit_dev_info(self, key: int, is_valid: bool, value: str):
        self._cb.put(lambda k=key, v=is_valid, s=value: _GUI_INSTANCE._on_dev_info_read(k, v, s))

    # ── main loop ──────────────────────────────────────────────────────────────

    def _run(self):
        while self._running:
            task = self._q.get()
            if task is None:
                break
            self._process(task)

    def _process(self, task: tuple):
        op = task[0]
        try:
            if op == "connect":
                transport = task[1]
                transport.connect()
                self._transport = transport
                self._emit_connected(True, "Connected")
                self._emit_log("Connected successfully", "info")

            elif op == "disconnect":
                if self._transport:
                    self._transport.disconnect()
                    self._transport = None
                self._emit_connected(False, "Disconnected")
                self._emit_log("Disconnected", "info")

            elif op == "read_key":
                key, hint_type = task[1], task[2]
                if not self._transport:
                    raise TransportError("Not connected")
                self._emit_log(f"READ  0x{key:04X} ...", "info")
                t, data = self._transport.get_value(key)
                self._emit_key_read(key, t, data)
                self._emit_log(f"READ  0x{key:04X} = {decode_value(data, t)!r}", "info")

            elif op == "read_all":
                if not self._transport:
                    raise TransportError("Not connected")
                keys = task[1]
                for i, (key, name, type_id, _grp) in enumerate(keys):
                    self._emit_progress(i, len(keys))
                    try:
                        t, data = self._transport.get_value(key)
                        self._emit_key_read(key, t, data)
                        self._emit_log(f"READ  0x{key:04X}  {name:<24} = {decode_value(data, t)!r}", "info")
                    except Exception as e:
                        self._emit_log(f"READ  0x{key:04X}  {name}  ERROR: {e}", "error")
                        if isinstance(e, TransportError):
                            self._emit_log("Aborting batch — device unreachable", "warn")
                            break
                self._emit_progress(len(keys), len(keys))
                self._emit_batch_done()

            elif op == "write_key":
                key, type_id, value_str = task[1], task[2], task[3]
                if not self._transport:
                    raise TransportError("Not connected")
                data = encode_value(value_str, type_id)
                self._emit_log(f"WRITE 0x{key:04X} = {value_str!r}", "info")
                self._transport.set_value(key, type_id, data)
                self._emit_write_done(key)
                self._emit_log(f"WRITE 0x{key:04X} OK", "info")

            elif op == "write_all":
                if not self._transport:
                    raise TransportError("Not connected")
                entries = task[1]
                for i, (key, type_id, value_str) in enumerate(entries):
                    self._emit_progress(i, len(entries))
                    try:
                        data = encode_value(value_str, type_id)
                        self._transport.set_value(key, type_id, data)
                        self._emit_write_done(key)
                        self._emit_log(f"WRITE 0x{key:04X} = {value_str!r}  OK", "info")
                    except Exception as e:
                        self._emit_log(f"WRITE 0x{key:04X}  ERROR: {e}", "error")
                        if isinstance(e, TransportError):
                            self._emit_log("Aborting batch", "warn")
                            break
                self._emit_progress(len(entries), len(entries))
                self._emit_batch_done()

            elif op == "read_dev_info":
                key = task[1]
                if not self._transport:
                    raise TransportError("Not connected")
                self._emit_log(f"DEV INFO READ 0x{key:04X} ...", "info")
                is_valid, value = self._transport.get_dev_info_value(key)
                self._emit_dev_info(key, is_valid, value)
                self._emit_log(f"DEV INFO 0x{key:04X} = {value!r}", "info")

        except Exception as e:
            if op == "connect":
                self._emit_connected(False, str(e))
                self._emit_log(f"Connection failed: {e}", "error")
            else:
                self._emit_log(f"Error [{op}]: {e}", "error")


# ─────────────────────────────────────────────────────────────────────────────
# Main window
# ─────────────────────────────────────────────────────────────────────────────

# Module-level reference set once the main window is created,
# used by ConfigWorker callbacks to reach the GUI.
_GUI_INSTANCE: Optional["FerpDeviceTool"] = None


class FerpDeviceTool(tk.Tk):

    # Dev info keys (matching app_device_info.h)
    _DEV_INFO_KEYS = DEV_INFO_KEYS

    def __init__(self):
        global _GUI_INSTANCE
        super().__init__()
        _GUI_INSTANCE = self

        self.title("FERP Device Tool")
        self.minsize(1000, 700)
        self.resizable(True, True)

        self._devices      = _load_devices()
        self._network_cfg  = _load_network_config()
        self._settings     = _load_settings()
        self._connected    = False

        # Raw MQTT client for message tree (FERP MQTT Tool mode)
        self._mqtt_raw = MqttRawClient(
            on_log=self._append_log,
            on_status_change=self._on_raw_mqtt_status,
        )
        self._raw_connected = False
        self._active_transport = None  # set when WebAPI is connected

        # Config worker + callback queue
        self._cb_queue = _WorkerEvent()
        self._worker   = ConfigWorker(self._cb_queue)

        self._build_ui()
        self._load_settings_to_ui()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        # Poll callback queue every 50 ms
        self._poll()

    # ── polling ────────────────────────────────────────────────────────────────

    def _poll(self):
        self._cb_queue.flush(self)
        self.after(50, self._poll)

    # ── UI construction ────────────────────────────────────────────────────────

    def _build_ui(self):
        self._build_connection_bar()
        self._build_devinfo_bar()

        body = ttk.PanedWindow(self, orient="horizontal")
        body.pack(fill="both", expand=True, padx=8, pady=4)

        # Left pane: message tree + command panel
        left = ttk.PanedWindow(body, orient="vertical")
        body.add(left, weight=2)

        self._msg_tree = MessageTree(left, on_select=self._on_msg_selected)
        left.add(self._msg_tree, weight=3)

        self._cmd_panel = CommandPanel(left, get_sender_fn=self._get_raw_client)
        left.add(self._cmd_panel, weight=1)

        # Right pane: OTA on top, config keys below
        right = ttk.PanedWindow(body, orient="vertical")
        body.add(right, weight=3)

        self._ota_panel = OtaPanel(
            right,
            get_ota_params=self._get_ota_params,
            append_log=self._append_log,
            get_ota_targets=self._get_ota_targets,
        )
        right.add(self._ota_panel, weight=1)

        self._config_panel = ConfigKeysPanel(
            right,
            on_read_key=self._on_read_key,
            on_write_key=self._on_write_key,
            on_read_all=self._on_read_all,
            on_write_all=self._on_write_all,
        )
        right.add(self._config_panel, weight=2)

        self._build_console()

    # ── Connection bar ─────────────────────────────────────────────────────────

    def _build_connection_bar(self):
        conn = ttk.LabelFrame(self, text="Connection")
        conn.pack(fill="x", padx=8, pady=(8, 2))

        # Interface selector
        row0 = ttk.Frame(conn)
        row0.pack(fill="x", padx=6, pady=(4, 2))

        ttk.Label(row0, text="Interface:").pack(side="left")
        self._iface_var = tk.StringVar(value="MQTT")
        self._iface_cb  = ttk.Combobox(row0, textvariable=self._iface_var,
                                        values=["MQTT", "WebAPI", "UART (future)"],
                                        width=14, state="readonly")
        self._iface_cb.pack(side="left", padx=(4, 12))
        self._iface_cb.bind("<<ComboboxSelected>>", self._on_iface_changed)

        # Interface-specific config — uses a notebook-style hidden frames approach
        self._iface_frame = ttk.Frame(row0)
        self._iface_frame.pack(side="left", fill="x", expand=True)

        self._mqtt_frame   = self._build_mqtt_frame(self._iface_frame)
        self._webapi_frame = self._build_webapi_frame(self._iface_frame)
        self._uart_frame   = self._build_uart_frame(self._iface_frame)
        self._mqtt_frame.pack(fill="x")  # default visible

        # Connect button + status dot
        self._connect_btn = ttk.Button(row0, text="Connect", command=self._do_connect, width=10)
        self._connect_btn.pack(side="right", padx=(8, 0))
        self._status_lbl = ttk.Label(row0, text="● Disconnected", foreground="red")
        self._status_lbl.pack(side="right", padx=(0, 8))

    def _build_mqtt_frame(self, parent) -> ttk.Frame:
        f = ttk.Frame(parent)
        _mqtt_cfg     = _net_mqtt(self._network_cfg)
        _brokers      = _mqtt_cfg.get("brokers", ["localhost"])
        _default_port = int(_mqtt_cfg.get("default_port", 1883))

        self._mqtt_cmd_prefix  = _mqtt_cfg.get("cmd_topic_prefix",  "ferp/ferp-com")
        self._mqtt_resp_prefix = _mqtt_cfg.get("resp_topic_prefix", "ferp/ferp-com")

        ttk.Label(f, text="Device:").pack(side="left")
        self._mqtt_device = ttk.Combobox(f, width=22, state="readonly")
        self._mqtt_device.pack(side="left", padx=(4, 8))
        self._mqtt_device["values"] = ["— select —"] + [
            d.get("label", d.get("mac", "?")) for d in self._devices
        ]
        self._mqtt_device.current(0)
        self._mqtt_device.bind("<<ComboboxSelected>>", self._on_device_mqtt_selected)
        ttk.Button(f, text="+Device", command=self._add_device, width=7).pack(side="left", padx=(0, 8))

        ttk.Label(f, text="Broker:").pack(side="left")
        self._mqtt_broker = ttk.Combobox(f, width=18)
        self._mqtt_broker["values"] = _brokers
        self._mqtt_broker.set(_brokers[0] if _brokers else "localhost")
        self._mqtt_broker.pack(side="left", padx=(4, 4))
        ttk.Button(f, text="+Broker", command=self._add_broker, width=7).pack(side="left", padx=(0, 8))

        ttk.Label(f, text="Port:").pack(side="left")
        self._mqtt_port = tk.IntVar(value=_default_port)
        ttk.Spinbox(f, textvariable=self._mqtt_port, from_=1, to=65535, width=6).pack(side="left", padx=(4, 8))

        self._mqtt_cmd_topic  = tk.StringVar(value=f"{self._mqtt_cmd_prefix}/cmd")
        self._mqtt_resp_topic = tk.StringVar(value=f"{self._mqtt_resp_prefix}/resp")
        return f

    def _build_webapi_frame(self, parent) -> ttk.Frame:
        f = ttk.Frame(parent)
        _wa_cfg  = _net_webapi(self._network_cfg)
        _wa_port = int(_wa_cfg.get("default_port", 8080))

        ttk.Label(f, text="Device:").pack(side="left")
        self._webapi_device = ttk.Combobox(f, width=22, state="readonly")
        self._webapi_device.pack(side="left", padx=(4, 8))
        self._webapi_device["values"] = ["— select —"] + [
            d.get("label", d.get("ip", "?")) for d in self._devices
        ]
        self._webapi_device.current(0)
        self._webapi_device.bind("<<ComboboxSelected>>", self._on_device_webapi_selected)
        ttk.Button(f, text="+Device", command=self._add_device, width=7).pack(side="left", padx=(0, 8))

        ttk.Label(f, text="IP:").pack(side="left")
        self._webapi_ip = tk.StringVar(value="192.168.4.1")
        ttk.Entry(f, textvariable=self._webapi_ip, width=18).pack(side="left", padx=(4, 8))

        ttk.Label(f, text="Port:").pack(side="left")
        self._webapi_port = tk.IntVar(value=_wa_port)
        ttk.Spinbox(f, textvariable=self._webapi_port, from_=1, to=65535, width=6).pack(side="left", padx=(4, 0))
        return f

    def _build_uart_frame(self, parent) -> ttk.Frame:
        f = ttk.Frame(parent)
        default_port = (
            "/dev/tty.usbserial-A5069RR4"
            if platform.system() == "Darwin"
            else ("COM3" if platform.system() == "Windows" else "/dev/ttyUSB0")
        )
        ttk.Label(f, text="Port:").pack(side="left")
        self._uart_port = tk.StringVar(value=default_port)
        ttk.Entry(f, textvariable=self._uart_port, width=24).pack(side="left", padx=(4, 4))
        ttk.Button(f, text="Scan", command=self._scan_uart, width=5).pack(side="left", padx=(0, 8))

        ttk.Label(f, text="Baud:").pack(side="left")
        self._uart_baud = tk.StringVar(value="115200")
        ttk.Combobox(f, textvariable=self._uart_baud,
                     values=["9600","19200","38400","57600","115200","230400","460800","921600"],
                     width=9, state="readonly").pack(side="left", padx=(4, 0))

        ttk.Label(f, text="(UART: config read/write only)", foreground="gray").pack(side="left", padx=8)
        return f

    def _on_iface_changed(self, _=None):
        for frm in (self._mqtt_frame, self._webapi_frame, self._uart_frame):
            frm.pack_forget()
        iface = self._iface_var.get()
        if iface == "MQTT":
            self._mqtt_frame.pack(fill="x")
        elif iface == "WebAPI":
            self._webapi_frame.pack(fill="x")
        else:
            self._uart_frame.pack(fill="x")
        # Reload OTA target list for the newly selected interface
        self._ota_panel.refresh_targets()

    # ── Device Info bar ────────────────────────────────────────────────────────

    def _build_devinfo_bar(self):
        box = ttk.LabelFrame(self, text="Device Info")
        box.pack(fill="x", padx=8, pady=(2, 4))
        inner = ttk.Frame(box)
        inner.pack(fill="x", padx=6, pady=(2, 4))

        self._devinfo_vars:  Dict[int, tk.StringVar] = {}
        self._devinfo_btns:  Dict[int, ttk.Button]   = {}

        for key, label, _ in self._DEV_INFO_KEYS:
            ttk.Label(inner, text=f"{label}:").pack(side="left")
            var = tk.StringVar(value="—")
            self._devinfo_vars[key] = var
            value_lbl = ttk.Label(inner, textvariable=var, width=24,
                                  relief="groove", anchor="w", padding=(4, 1))
            value_lbl.pack(side="left", padx=(4, 2))
            btn = ttk.Button(inner, text="↺", width=2,
                             command=lambda k=key: self._on_read_dev_info(k),
                             state="disabled")
            self._devinfo_btns[key] = btn
            btn.pack(side="left", padx=(0, 10))

        self._devinfo_all_btn = ttk.Button(inner, text="↺ All", width=5,
                                            command=self._on_read_all_dev_info,
                                            state="disabled")
        self._devinfo_all_btn.pack(side="left")

    # ── Console ────────────────────────────────────────────────────────────────

    def _build_console(self):
        lf = ttk.LabelFrame(self, text="Console")
        lf.pack(fill="x", padx=8, pady=(0, 8))
        self._console = scrolledtext.ScrolledText(
            lf, height=9, wrap="word", state="disabled", font=("Courier", 11))
        self._console.pack(fill="both", expand=True, padx=4, pady=4)
        for tag, color in [
            ("cmd",   "#b8860b"),
            ("resp",  "#228b22"),
            ("evt",   "#1e90ff"),
            ("info",  "#555555"),
            ("warn",  "#cc6600"),
            ("error", "#cc0000"),
        ]:
            self._console.tag_config(tag, foreground=color)
        ttk.Button(lf, text="Clear console", command=self._clear_console).pack(
            side="right", padx=4, pady=(0, 4))
        ttk.Button(lf, text="Copy console", command=self._copy_console).pack(
            side="right", padx=(4, 0), pady=(0, 4))

    # ── Connection actions ─────────────────────────────────────────────────────

    def _do_connect(self):
        iface = self._iface_var.get()

        if self._connected:
            # Disconnect
            self._worker.enqueue(("disconnect",))
            if self._raw_connected:
                self._mqtt_raw.disconnect()
            return

        try:
            if iface == "MQTT":
                host = self._mqtt_broker.get().strip()
                port = int(self._mqtt_port.get())
                cmd  = self._mqtt_cmd_topic.get().strip()
                resp = self._mqtt_resp_topic.get().strip()
                if not host:
                    messagebox.showerror("Missing", "Enter MQTT broker address.")
                    return
                self._append_log(f"MQTT cmd  topic: {cmd}",  "info")
                self._append_log(f"MQTT resp topic: {resp}", "info")
                transport = MQTTTransport(host, port, cmd, resp,
                                          log_fn=self._append_log)
                # Also connect raw MQTT client for message tree
                # Get MQTT base from cmd topic (strip /cmd suffix)
                base = cmd.rsplit("/cmd", 1)[0] if cmd.endswith("/cmd") else cmd
                self._mqtt_raw.connect(host, port, base)

            elif iface == "WebAPI":
                ip   = self._webapi_ip.get().strip()
                port = int(self._webapi_port.get())
                if not ip:
                    messagebox.showerror("Missing", "Enter device IP address.")
                    return
                transport = WebAPITransport(ip, port)
                self._active_transport = transport

            else:  # UART
                p    = self._uart_port.get().strip()
                baud = int(self._uart_baud.get())
                transport = UARTTransport(p, baud)

        except TransportError as e:
            self._append_log(str(e), "error")
            return

        self._connect_btn.config(state="disabled")
        self._append_log(f"Connecting via {iface} ...", "info")
        self._worker.enqueue(("connect", transport))

    def _on_connected(self, success: bool, msg: str):
        self._connected = success
        self._connect_btn.config(state="normal")
        if success:
            self._connect_btn.config(text="Disconnect")
            self._status_lbl.config(text="● Connected", foreground="green")
            self._config_panel.set_connected(True)
            for btn in self._devinfo_btns.values():
                btn.config(state="normal")
            self._devinfo_all_btn.config(state="normal")
        else:
            self._connect_btn.config(text="Connect")
            self._status_lbl.config(text="● Disconnected", foreground="red")
            self._active_transport = None
            self._config_panel.set_connected(False)
            for btn in self._devinfo_btns.values():
                btn.config(state="disabled")
            self._devinfo_all_btn.config(state="disabled")
            for var in self._devinfo_vars.values():
                var.set("")

    def _on_raw_mqtt_status(self, connected: bool):
        self._raw_connected = connected

    def _get_raw_client(self) -> Optional[MqttRawClient]:
        if self._raw_connected:
            return self._mqtt_raw
        if self._active_transport is not None:
            return WebApiSender(self._active_transport, self._append_log)
        return None

    # ── OTA params helper ──────────────────────────────────────────────────────

    def _get_ota_targets(self) -> List[Tuple[str, str]]:
        """Return [(label, name), ...] for the currently selected interface."""
        iface = self._iface_var.get().lower()  # "mqtt", "webapi", or "uart (future)"
        return _load_ota_targets(iface if iface in ("mqtt", "webapi") else "mqtt")

    def _get_ota_params(self):
        """Returns (iface, conn_dict) or None if not connected."""
        if not self._connected and not self._raw_connected:
            return None
        iface = self._iface_var.get()
        if iface == "MQTT":
            cmd = self._mqtt_cmd_topic.get().strip()
            # Derive dev_type, group, device_id from topic  ferp/{dev_type}/{group}/{id}/cmd
            parts = cmd.split("/")
            if len(parts) >= 5:
                dev_type  = parts[1]
                group     = parts[2]
                device_id = parts[3]
            else:
                dev_type, group, device_id = "ferp-com", "default", "unknown"
            return ("mqtt", {
                "broker":    self._mqtt_broker.get().strip(),
                "port":      int(self._mqtt_port.get()),
                "dev_type":  dev_type,
                "group":     group,
                "device_id": device_id,
            })
        elif iface == "WebAPI":
            return ("webapi", {
                "ip":   self._webapi_ip.get().strip(),
                "port": int(self._webapi_port.get()),
            })
        return None

    # ── Message tree ───────────────────────────────────────────────────────────

    def _on_msg_selected(self, msg_name: str):
        self._cmd_panel.load_message(msg_name)

    # ── Config read/write slots ────────────────────────────────────────────────

    def _on_read_key(self, key_id: int):
        if not self._connected:
            self._append_log("Not connected", "warn")
            return
        # Find type for this key
        type_id = next((t for k, _, t, _ in CONFIG_KEYS if k == key_id), TYPE_STRING)
        self._worker.enqueue(("read_key", key_id, type_id))

    def _on_write_key(self, key_id: int):
        # Write single key — get current value from config panel
        if not self._connected:
            self._append_log("Not connected", "warn")
            return
        for k, _, type_id, _ in CONFIG_KEYS:
            if k == key_id:
                entries = self._config_panel.get_all_values()
                for ek, et, ev in entries:
                    if ek == key_id:
                        self._worker.enqueue(("write_key", key_id, type_id, ev))
                        return

    def _on_read_all(self):
        if not self._connected:
            return
        self._worker.enqueue(("read_all", CONFIG_KEYS))

    def _on_write_all(self):
        if not self._connected:
            return
        entries = self._config_panel.get_all_values()
        self._worker.enqueue(("write_all", entries))

    def _on_key_read(self, key: int, type_id: int, data: list):
        self._config_panel.update_key(key, type_id, data)

    def _on_write_done(self, key: int):
        self._config_panel.mark_written(key)

    def _on_progress(self, current: int, total: int):
        self._config_panel.show_progress(current, total)

    def _on_batch_done(self):
        self._config_panel.show_progress(1, 1)  # hide

    # ── Device info ────────────────────────────────────────────────────────────

    def _on_read_dev_info(self, key: int):
        if not self._connected:
            return
        self._worker.enqueue(("read_dev_info", key))

    def _on_read_all_dev_info(self):
        if not self._connected:
            return
        for key, _, _ in self._DEV_INFO_KEYS:
            self._worker.enqueue(("read_dev_info", key))

    def _on_dev_info_read(self, key: int, is_valid: bool, value: str):
        var = self._devinfo_vars.get(key)
        if var is not None:
            var.set(value if is_valid else "—")

    # ── Device selection ───────────────────────────────────────────────────────

    def _on_device_mqtt_selected(self, _=None):
        idx = self._mqtt_device.current()
        if idx <= 0 or idx - 1 >= len(self._devices):
            return
        d     = self._devices[idx - 1]
        uuid  = d.get("uuid", "")
        mac   = d.get("mac", "")
        group = d.get("group", "default")
        dev_id = uuid if uuid else mac
        if dev_id:
            self._mqtt_cmd_topic.set(f"{self._mqtt_cmd_prefix}/{group}/{dev_id}/cmd")
            self._mqtt_resp_topic.set(f"{self._mqtt_resp_prefix}/{group}/{dev_id}/resp")
        for key, _, field in self._DEV_INFO_KEYS:
            var = self._devinfo_vars.get(key)
            if var is not None:
                var.set(d.get(field, "") or "—")
        self._append_log(f"Device selected: {d.get('label', dev_id or '?')}", "info")

    def _on_device_webapi_selected(self, _=None):
        idx = self._webapi_device.current()
        if idx <= 0 or idx - 1 >= len(self._devices):
            return
        d  = self._devices[idx - 1]
        ip = d.get("ip", "")
        if ip:
            self._webapi_ip.set(ip)
        for key, _, field in self._DEV_INFO_KEYS:
            var = self._devinfo_vars.get(key)
            if var is not None:
                var.set(d.get(field, "") or "—")
        self._append_log(f"Device selected: {d.get('label', ip or '?')}", "info")

    def _add_broker(self):
        dlg = _AddBrokerDialog(self)
        if not dlg.result:
            return
        broker, port = dlg.result
        cfg = _load_network_config()
        brokers = cfg.get("mqtt", {}).get("brokers", [])
        if broker and broker not in brokers:
            brokers.append(broker)
            cfg.setdefault("mqtt", {})["brokers"] = brokers
            _save_json(NETWORK_FILE, cfg)
        cur = list(self._mqtt_broker["values"])
        if broker not in cur:
            cur.append(broker)
            self._mqtt_broker["values"] = cur
        self._mqtt_broker.set(broker)

    def _add_device(self):
        dlg = _AddDeviceDialog(self)
        if not dlg.result:
            return
        dev = dlg.result
        self._devices.append(dev)
        # Save back to ferp_devices.json
        data = _load_json(DEVICES_FILE, {"devices": []})
        data.setdefault("devices", []).append(dev)
        _save_json(DEVICES_FILE, data)
        # Refresh combos
        labels = ["— select —"] + [d.get("label", d.get("mac", "?")) for d in self._devices]
        self._mqtt_device["values"]   = labels
        self._webapi_device["values"] = labels
        self._append_log(f"Device added: {dev.get('label', '?')}", "info")

    def _scan_uart(self):
        try:
            import serial.tools.list_ports
            ports = [p.device for p in serial.tools.list_ports.comports()]
            if ports:
                self._uart_port.set(ports[0])
                self._append_log("Available ports: " + ", ".join(ports), "info")
            else:
                self._append_log("No serial ports found", "warn")
        except ImportError:
            self._append_log("pyserial not installed", "error")

    # ── Console ────────────────────────────────────────────────────────────────

    def _append_log(self, text: str, tag: str = "info"):
        ts = time.strftime("%H:%M:%S")
        self.after(0, self._insert_log, ts, text, tag)

    def _insert_log(self, ts: str, text: str, tag: str):
        self._console.config(state="normal")
        self._console.insert("end", f"[{ts}] {text}\n", tag)
        self._console.see("end")
        self._console.config(state="disabled")

    def _clear_console(self):
        self._console.config(state="normal")
        self._console.delete("1.0", "end")
        self._console.config(state="disabled")

    def _copy_console(self):
        text = self._console.get("1.0", "end").strip()
        self.clipboard_clear()
        self.clipboard_append(text)

    # ── Settings ───────────────────────────────────────────────────────────────

    def _load_settings_to_ui(self):
        s = self._settings
        if not s:
            return
        iface = s.get("interface", "MQTT")
        if iface in ("MQTT", "WebAPI"):
            self._iface_var.set(iface)
            self._on_iface_changed()
        if s.get("mqtt_broker"):
            self._mqtt_broker.set(s["mqtt_broker"])
        if s.get("mqtt_port"):
            self._mqtt_port.set(int(s["mqtt_port"]))
        if s.get("mqtt_cmd"):
            self._mqtt_cmd_topic.set(s["mqtt_cmd"])
        if s.get("mqtt_resp"):
            self._mqtt_resp_topic.set(s["mqtt_resp"])
        if s.get("webapi_ip"):
            self._webapi_ip.set(s["webapi_ip"])
        if s.get("webapi_port"):
            self._webapi_port.set(int(s["webapi_port"]))
        if s.get("uart_port"):
            self._uart_port.set(s["uart_port"])
        if s.get("uart_baud"):
            self._uart_baud.set(str(s["uart_baud"]))

    def _collect_settings(self) -> dict:
        return {
            "interface":   self._iface_var.get(),
            "mqtt_broker": self._mqtt_broker.get(),
            "mqtt_port":   self._mqtt_port.get(),
            "mqtt_cmd":    self._mqtt_cmd_topic.get(),
            "mqtt_resp":   self._mqtt_resp_topic.get(),
            "webapi_ip":   self._webapi_ip.get(),
            "webapi_port": self._webapi_port.get(),
            "uart_port":   self._uart_port.get(),
            "uart_baud":   self._uart_baud.get(),
        }

    def _on_close(self):
        _save_settings(self._collect_settings())
        if self._raw_connected:
            self._mqtt_raw.disconnect()
        self._worker.stop()
        self.destroy()


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    FerpDeviceTool().mainloop()
