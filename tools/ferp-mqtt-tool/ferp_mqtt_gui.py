"""
ferp_mqtt_gui.py  —  FERP MQTT command / OTA tool (GUI)

Layout
------
  ┌─ Connection ──────────────────────────────────────────────────────┐
  └───────────────────────────────────────────────────────────────────┘
  ┌─ Left (PanedWindow vertical) ──────┬─ Right: OTA Upgrade ────────┐
  │  [🔍 filter]                       │  [Browse…] path/fw.bin       │
  │  ┌─ Message Tree ───────────────┐  │  Target▾  Version  Chunk▾   │
  │  │ ▸ Buttons (2)               │  │  ████░░░░  42%               │
  │  │   MsgDefaultBtn             │  │  [Start OTA] [Abort]         │
  │  │ ▸ Config (8)                │  │  ──── OTA Log ──────         │
  │  │   MsgConfigGet              │  │  ...                         │
  │  │ ▸ OTA …   ▸ Timer …         │  │                              │
  │  └──────────────────────────── ┘  └──────────────────────────────┘
  │  ┌─ Command Input ─────────────┐
  │  │ MsgConfigGetMqtt            │
  │  │ ID=0x0309 · cmd · 0 fields  │
  │  │ (no payload)  [Send]        │
  │  └─────────────────────────────┘
  ├─ Console (shared) ────────────────────────────────────────── [Clear] ┤
  └─────────────────────────────────────────────────────────────────────┘

Messages are loaded dynamically from src/app-messages/messages/**/*.json.
No edits required when adding a new message — just add its JSON file.
"""

import json
import os
import random
import sys
import time
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, simpledialog, filedialog

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERROR: paho-mqtt not installed.\nRun:  pip install paho-mqtt", file=sys.stderr)
    sys.exit(1)

from messages.msg_loader import MSG_DEFS, CMD_MSGS, RESP_MSGS
from messages.ota_session import OtaSession

# ---------------------------------------------------------------------------
# Config helpers
# ---------------------------------------------------------------------------

CONFIG_FILE = os.path.join(os.path.dirname(__file__), "ferp_mqtt_config.json")

DEFAULT_CONFIG = {
    "brokers": ["broker.emqx.io", "localhost"],
    "ports":   [1883, 8883],
    "devices": [
        {"id": "00000000-0000-0000-0000-000000000000", "dev_type": "ferp-com", "group": "default"}
    ],
}


def load_config() -> dict:
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r") as fh:
                cfg = json.load(fh)
            for k, v in DEFAULT_CONFIG.items():
                cfg.setdefault(k, v)
            return cfg
        except (json.JSONDecodeError, OSError):
            pass
    return dict(DEFAULT_CONFIG)


def save_config(cfg: dict) -> None:
    with open(CONFIG_FILE, "w") as fh:
        json.dump(cfg, fh, indent=2)


# ---------------------------------------------------------------------------
# Topic helpers
# ---------------------------------------------------------------------------

def build_topic_base(dev_type: str, group: str, device_id: str) -> str:
    safe_id = device_id.replace(":", "").replace("-", "").lower()
    return f"ferp/{dev_type}/{group}/{safe_id}"

def cmd_topic(base: str)  -> str: return f"{base}/cmd"
def resp_topic(base: str) -> str: return f"{base}/resp"
def evt_topic(base: str)  -> str: return f"{base}/evt"


# ---------------------------------------------------------------------------
# MQTT client
# ---------------------------------------------------------------------------

class MqttClient:
    def __init__(self, on_log, on_status_change):
        self._on_log    = on_log
        self._on_status = on_status_change
        self._client    = None
        self._base      = ""
        self._connected = False

    def connect(self, broker: str, port: int, base: str) -> None:
        if self._client:
            self._client.disconnect()
            self._client.loop_stop()
        self._base = base
        cid = f"ferp-gui-{random.randint(1000, 9999)}"
        self._client = mqtt.Client(client_id=cid)
        self._client.on_connect    = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message    = self._on_message
        try:
            self._client.connect(broker, port, keepalive=60)
            self._client.loop_start()
        except Exception as exc:
            self._on_log(f"[error] Could not connect: {exc}", "error")
            self._on_status(False)

    def disconnect(self) -> None:
        if self._client:
            self._client.disconnect()
            self._client.loop_stop()
            self._client = None
        self._connected = False
        self._on_status(False)

    def send(self, msg_name: str, data: dict) -> None:
        if not self._client or not self._connected:
            self._on_log("[error] Not connected", "error")
            return
        seq     = random.randint(1, 0xFFFF_FFFF)
        payload = {"seq": seq, "msg": msg_name, "data": data}
        self._on_log(f"\u2192 [cmd]  {json.dumps(payload)}", "cmd")
        self._client.publish(cmd_topic(self._base), json.dumps(payload), qos=1)

    @property
    def is_connected(self) -> bool:
        return self._connected

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self._connected = True
            self._on_status(True)
            self._on_log(f"[info]  Connected  base={self._base}", "info")
            client.subscribe(resp_topic(self._base), qos=1)
            client.subscribe(evt_topic(self._base), qos=0)
            self._on_log("[info]  Subscribed to resp + evt", "info")
        else:
            self._on_log(f"[error] Connection refused rc={rc}", "error")
            self._on_status(False)

    def _on_disconnect(self, client, userdata, rc):
        self._connected = False
        self._on_status(False)
        msg = f"[warn]  Unexpected disconnect rc={rc}" if rc != 0 else "[info]  Disconnected"
        self._on_log(msg, "warn" if rc != 0 else "info")

    def _on_message(self, client, userdata, message):
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception as exc:
            self._on_log(f"[warn]  Bad payload on {message.topic}: {exc}", "warn")
            return
        suffix = message.topic.split("/")[-1]
        self._on_log(f"\u2190 [{suffix}]  {json.dumps(payload, indent=2)}", suffix)


# ---------------------------------------------------------------------------
# Dialogs
# ---------------------------------------------------------------------------

class AddBrokerDialog(simpledialog.Dialog):
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


class AddDeviceDialog(simpledialog.Dialog):
    def body(self, master):
        self.title("Add device")
        self.vars = []
        for i, (lbl, dflt) in enumerate(
            [("Device ID:", "AA:BB:CC:DD:EE:FF"), ("Dev-type:", "ferp-com"), ("Group:", "default")]
        ):
            tk.Label(master, text=lbl).grid(row=i, column=0, sticky="w")
            v = tk.StringVar(value=dflt)
            tk.Entry(master, textvariable=v, width=30).grid(row=i, column=1, padx=4)
            self.vars.append(v)
        return None

    def apply(self):
        self.result = {
            "id":       self.vars[0].get().strip(),
            "dev_type": self.vars[1].get().strip(),
            "group":    self.vars[2].get().strip(),
        }


# ---------------------------------------------------------------------------
# Left panel: message tree
# ---------------------------------------------------------------------------

# Messages grouped by folder; tree shows ALL messages (resp shown read-only)
_GROUPS: dict[str, list[str]] = {}
for _n, _d in sorted(MSG_DEFS.items()):
    _g = _d.get("group", "Other")
    _GROUPS.setdefault(_g, []).append(_n)


class MessageTree(ttk.Frame):
    """Treeview showing messages grouped by JSON subdirectory (folder = group)."""

    def __init__(self, parent, on_select, **kw):
        super().__init__(parent, **kw)
        self._on_select = on_select
        self._filter_var = tk.StringVar()
        self._filter_var.trace_add("write", self._apply_filter)
        self._build_ui()

    def _build_ui(self):
        # Filter entry
        filter_frame = ttk.Frame(self)
        filter_frame.pack(fill="x", padx=4, pady=(4, 2))
        ttk.Label(filter_frame, text="\U0001f50d").pack(side="left")
        ttk.Entry(filter_frame, textvariable=self._filter_var).pack(
            side="left", fill="x", expand=True, padx=(4, 0))

        # Treeview + scrollbar
        tv_frame = ttk.Frame(self)
        tv_frame.pack(fill="both", expand=True, padx=4, pady=(0, 4))

        self._tv = ttk.Treeview(tv_frame, columns=("dir",), show="tree headings",
                                 selectmode="browse")
        self._tv.heading("#0",  text="Message")
        self._tv.heading("dir", text="Dir")
        self._tv.column("#0",   width=180, stretch=True)
        self._tv.column("dir",  width=40,  stretch=False, anchor="center")

        scroll = ttk.Scrollbar(tv_frame, orient="vertical", command=self._tv.yview)
        self._tv.configure(yscrollcommand=scroll.set)
        self._tv.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")

        # Colour tags
        self._tv.tag_configure("cmd",  foreground="#0066cc")
        self._tv.tag_configure("resp", foreground="#228b22")
        self._tv.tag_configure("any",  foreground="#7a5f00")
        self._tv.tag_configure("group", font=("TkDefaultFont", 10, "bold"))

        self._tv.bind("<<TreeviewSelect>>", self._on_tree_select)
        self._populate()

    def _populate(self, filter_text: str = ""):
        flt = filter_text.lower().strip()
        for item in self._tv.get_children():
            self._tv.delete(item)

        for group, names in sorted(_GROUPS.items()):
            matched = [n for n in names if flt in n.lower()] if flt else list(names)
            if not matched:
                continue
            gid = self._tv.insert("", "end", text=f"{group}  ({len(matched)})",
                                   open=True, tags=("group",))
            for name in sorted(matched):
                d   = MSG_DEFS[name]
                tag = d["direction"]
                lbl = name[3:] if name.startswith("Msg") else name   # strip leading Msg
                self._tv.insert(gid, "end", iid=name, text=lbl,
                                 values=(d["direction"],), tags=(tag,))

    def _apply_filter(self, *_):
        self._populate(self._filter_var.get())

    def _on_tree_select(self, _event=None):
        sel = self._tv.selection()
        if not sel:
            return
        iid = sel[0]
        if iid in MSG_DEFS:
            self._on_select(iid)


# ---------------------------------------------------------------------------
# Command input panel (left bottom)
# ---------------------------------------------------------------------------

class CommandPanel(ttk.LabelFrame):
    """Shows fields for the currently selected message; has a Send button."""

    def __init__(self, parent, mqtt_client: MqttClient, **kw):
        super().__init__(parent, text="Command", **kw)
        self._mqtt    = mqtt_client
        self._msg_name: str = ""
        self._widgets: list[tuple[str, tk.Variable, dict]] = []
        self._build_ui()

    def _build_ui(self):
        self._info_lbl = ttk.Label(self, text="Select a message in the tree",
                                    foreground="#666666")
        self._info_lbl.pack(anchor="w", padx=6, pady=(4, 0))

        self._fields_frame = ttk.Frame(self)
        self._fields_frame.pack(fill="x", padx=6, pady=4)

        bar = ttk.Frame(self)
        bar.pack(fill="x", padx=6, pady=(0, 6))
        self._send_btn = ttk.Button(bar, text="Send", command=self._do_send,
                                     state="disabled")
        self._send_btn.pack(side="left")
        self._dir_lbl = ttk.Label(bar, text="", foreground="#888888")
        self._dir_lbl.pack(side="left", padx=8)

    def load_message(self, msg_name: str):
        self._msg_name = msg_name
        defn = MSG_DEFS.get(msg_name, {})
        direction = defn.get("direction", "any")
        msg_id    = defn.get("msg_id", 0)
        fields    = defn.get("fields", [])

        self._info_lbl.config(
            text=f"{msg_name}   \u2502   ID=0x{msg_id:04X}   \u2502   {len(fields)} field(s)"
        )

        # Indicate direction
        sendable = direction in ("cmd", "any")
        dir_colors = {"cmd": "#0066cc", "resp": "#228b22", "any": "#7a5f00"}
        self._dir_lbl.config(text=f"[{direction}]",
                              foreground=dir_colors.get(direction, "#444444"))
        self._send_btn.config(state="normal" if sendable else "disabled")

        # Rebuild fields
        for w in self._fields_frame.winfo_children():
            w.destroy()
        self._widgets.clear()

        if not fields:
            ttk.Label(self._fields_frame, text="(no payload fields \u2014 empty {} sent)",
                      foreground="gray").grid(row=0, column=0, padx=4, sticky="w")
            return

        for col, fdef in enumerate(fields):
            name    = fdef["name"]
            label   = fdef.get("label", name)
            ftype   = fdef.get("type", "string")
            default = fdef.get("default", "")
            options = fdef.get("options", [])

            ttk.Label(self._fields_frame, text=f"{label}:").grid(
                row=0, column=col * 2, sticky="w", padx=(0, 2), pady=2)

            if ftype == "enum" and options:
                labels = [str(o["label"]) for o in options]
                var    = tk.StringVar(value=labels[0] if labels else "")
                for opt in options:
                    if str(opt.get("value", "")) == str(default):
                        var.set(str(opt["label"]))
                        break
                ttk.Combobox(self._fields_frame, textvariable=var,
                             values=labels, width=14, state="readonly").grid(
                    row=0, column=col * 2 + 1, sticky="w", padx=(0, 10))
            elif ftype == "bool":
                var = tk.StringVar(value="true" if default else "false")
                ttk.Combobox(self._fields_frame, textvariable=var,
                             values=["true", "false"], width=8, state="readonly").grid(
                    row=0, column=col * 2 + 1, sticky="w", padx=(0, 10))
            else:
                var = tk.StringVar(value=str(default) if default != "" else "")
                ttk.Entry(self._fields_frame, textvariable=var, width=16).grid(
                    row=0, column=col * 2 + 1, sticky="w", padx=(0, 10))

            self._widgets.append((name, var, fdef))

    def _do_send(self):
        if not self._msg_name:
            return
        data: dict = {}
        for name, var, fdef in self._widgets:
            raw   = var.get().strip()
            ftype = fdef.get("type", "string")
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
                    messagebox.showerror("Bad value",
                                         f"Field '{name}' expects integer, got: {raw!r}")
                    return
            elif ftype in ("float", "double"):
                try:
                    data[name] = float(raw) if raw else 0.0
                except ValueError:
                    messagebox.showerror("Bad value",
                                         f"Field '{name}' expects number, got: {raw!r}")
                    return
            else:
                data[name] = raw
        self._mqtt.send(self._msg_name, data)


# ---------------------------------------------------------------------------
# OTA panel (right side)
# ---------------------------------------------------------------------------

class OtaPanel(ttk.LabelFrame):
    """OTA firmware update controls — right side of the main window."""

    def __init__(self, parent, get_connection_info, append_log, **kw):
        kw.setdefault("text", "OTA Upgrade")
        super().__init__(parent, **kw)
        self._get_conn   = get_connection_info
        self._append_log = append_log
        self._session    = None
        self._fw_path    = tk.StringVar()
        self._target_var = tk.StringVar(value="main")
        self._ver_var    = tk.StringVar(value="1.0.0")
        self._chunk_var  = tk.StringVar(value="4096")
        self._build_ui()

    def _build_ui(self):
        pad = {"padx": 6, "pady": 3}

        # Firmware picker
        fw = ttk.Frame(self)
        fw.pack(fill="x", padx=6, pady=(6, 2))
        ttk.Button(fw, text="Browse\u2026", command=self._browse).pack(side="left")
        ttk.Entry(fw, textvariable=self._fw_path, state="readonly").pack(
            side="left", fill="x", expand=True, padx=(6, 0))

        # Options
        opt = ttk.Frame(self)
        opt.pack(fill="x", padx=6, pady=2)
        ttk.Label(opt, text="Target:").pack(side="left")
        ttk.Combobox(opt, textvariable=self._target_var, values=["main", "sub1"],
                     width=8, state="readonly").pack(side="left", padx=(4, 12))
        ttk.Label(opt, text="Version:").pack(side="left")
        ttk.Entry(opt, textvariable=self._ver_var, width=10).pack(side="left", padx=(4, 12))
        ttk.Label(opt, text="Chunk:").pack(side="left")
        ttk.Combobox(opt, textvariable=self._chunk_var,
                     values=["1024", "2048", "4096", "8192"], width=7).pack(
            side="left", padx=(4, 0))

        # Progress
        pf = ttk.Frame(self)
        pf.pack(fill="x", padx=6, pady=2)
        self._progress = ttk.Progressbar(pf, mode="determinate")
        self._progress.pack(side="left", fill="x", expand=True)
        self._pct_lbl = ttk.Label(pf, text="  0%", width=6)
        self._pct_lbl.pack(side="left")

        # Buttons
        bf = ttk.Frame(self)
        bf.pack(fill="x", padx=6, pady=2)
        self._start_btn = ttk.Button(bf, text="Start OTA", command=self._start)
        self._start_btn.pack(side="left", padx=(0, 8))
        self._abort_btn = ttk.Button(bf, text="Abort", command=self._abort, state="disabled")
        self._abort_btn.pack(side="left")

        # OTA-specific log
        lf = ttk.LabelFrame(self, text="OTA Log")
        lf.pack(fill="both", expand=True, padx=6, pady=(4, 6))
        self._log = scrolledtext.ScrolledText(lf, height=8, wrap="word",
                                               state="disabled", font=("Courier", 10))
        self._log.pack(fill="both", expand=True, padx=4, pady=4)
        self._log.tag_config("error", foreground="#cc0000")
        self._log.tag_config("warn",  foreground="#cc6600")
        self._log.tag_config("ok",    foreground="#228b22")
        ttk.Button(lf, text="Clear", command=self._clear_log).pack(
            side="right", padx=4, pady=(0, 4))

    def _browse(self):
        path = filedialog.askopenfilename(
            title="Select firmware .bin",
            filetypes=[("Binary firmware", "*.bin"), ("All files", "*.*")])
        if path:
            self._fw_path.set(path)

    def _start(self):
        broker, port, dev_type, group, dev_id = self._get_conn()
        if not broker or not dev_id:
            messagebox.showerror("Not configured",
                                  "Fill in connection details before starting OTA.")
            return
        fw = self._fw_path.get().strip()
        if not fw:
            messagebox.showerror("No firmware", "Select a firmware .bin file first.")
            return
        try:
            chunk = int(self._chunk_var.get())
        except ValueError:
            chunk = 4096
        self._log_ota(f"Starting OTA \u2014 {broker}:{port}  device={dev_id}", "ok")
        self._progress["value"] = 0
        self._pct_lbl.config(text="  0%")
        self._start_btn.config(state="disabled")
        self._abort_btn.config(state="normal")
        self._session = OtaSession(
            broker=broker, port=port,
            dev_type=dev_type, group=group, device_id=dev_id,
            firmware_path=fw, target=self._target_var.get(),
            version=self._ver_var.get().strip() or "unknown",
            chunk_size=chunk,
            on_log=self._cb_log,
            on_progress=self._cb_progress,
            on_done=self._cb_done,
        )
        self._session.start()

    def _abort(self):
        if self._session:
            self._session.abort()
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
        self._session = None
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


# ---------------------------------------------------------------------------
# Main window
# ---------------------------------------------------------------------------

class FerpMqttGui(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("FERP MQTT Tool")
        self.minsize(900, 640)
        self.resizable(True, True)
        self._cfg  = load_config()
        self._mqtt = MqttClient(on_log=self._append_log,
                                 on_status_change=self._on_status_change)
        self._build_ui()
        self._populate_config()
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ── Build UI ──────────────────────────────────────────────────────────────

    def _build_ui(self):
        # 1. Connection bar (top, fixed height)
        self._build_connection_bar()

        # 2. Main body: horizontal paned window
        body = ttk.PanedWindow(self, orient="horizontal")
        body.pack(fill="both", expand=True, padx=8, pady=4)

        # -- Left side: vertical PanedWindow (tree top, command input bottom)
        left_pane = ttk.PanedWindow(body, orient="vertical")
        body.add(left_pane, weight=2)

        self._tree = MessageTree(left_pane, on_select=self._on_msg_selected)
        left_pane.add(self._tree, weight=3)

        self._cmd_panel = CommandPanel(left_pane, self._mqtt)
        left_pane.add(self._cmd_panel, weight=1)

        # -- Right side: OTA panel
        self._ota_panel = OtaPanel(body,
                                    get_connection_info=self._get_conn_info,
                                    append_log=self._append_log)
        body.add(self._ota_panel, weight=1)

        # 3. Console (bottom, fixed)
        self._build_console()

    def _build_connection_bar(self):
        pad = {"padx": 5, "pady": 3}
        conn = ttk.LabelFrame(self, text="Connection")
        conn.pack(fill="x", padx=8, pady=(8, 2))

        # Row 0: broker + port
        ttk.Label(conn, text="Broker:").grid(row=0, column=0, sticky="w", **pad)
        self._broker_var = tk.StringVar()
        self._broker_cb  = ttk.Combobox(conn, textvariable=self._broker_var, width=22)
        self._broker_cb.grid(row=0, column=1, sticky="w", **pad)

        ttk.Label(conn, text="Port:").grid(row=0, column=2, sticky="w", **pad)
        self._port_var = tk.StringVar()
        self._port_cb  = ttk.Combobox(conn, textvariable=self._port_var, width=7)
        self._port_cb.grid(row=0, column=3, sticky="w", **pad)

        ttk.Button(conn, text="+Broker", command=self._add_broker).grid(
            row=0, column=4, **pad)

        # Row 1: device
        ttk.Label(conn, text="Device ID:").grid(row=1, column=0, sticky="w", **pad)
        self._device_var = tk.StringVar()
        self._device_cb  = ttk.Combobox(conn, textvariable=self._device_var, width=22)
        self._device_cb.grid(row=1, column=1, sticky="w", **pad)
        self._device_cb.bind("<<ComboboxSelected>>", self._on_device_selected)

        ttk.Label(conn, text="Dev-type:").grid(row=1, column=2, sticky="w", **pad)
        self._devtype_var = tk.StringVar()
        ttk.Entry(conn, textvariable=self._devtype_var, width=12).grid(
            row=1, column=3, sticky="w", **pad)

        ttk.Label(conn, text="Group:").grid(row=1, column=4, sticky="w", **pad)
        self._group_var = tk.StringVar()
        ttk.Entry(conn, textvariable=self._group_var, width=12).grid(
            row=1, column=5, sticky="w", **pad)

        ttk.Button(conn, text="+Device", command=self._add_device).grid(
            row=1, column=6, **pad)

        # Row 2: connect/disconnect + status
        bar = ttk.Frame(conn)
        bar.grid(row=2, column=0, columnspan=7, sticky="w", **pad)
        self._connect_btn = ttk.Button(bar, text="Connect", command=self._do_connect)
        self._connect_btn.pack(side="left", padx=(0, 6))
        self._disconnect_btn = ttk.Button(bar, text="Disconnect",
                                           command=self._do_disconnect, state="disabled")
        self._disconnect_btn.pack(side="left", padx=(0, 12))
        self._status_lbl = ttk.Label(bar, text="\u25cf Disconnected", foreground="red")
        self._status_lbl.pack(side="left")

    def _build_console(self):
        lf = ttk.LabelFrame(self, text="Console")
        lf.pack(fill="x", padx=8, pady=(0, 8))
        self._console = scrolledtext.ScrolledText(
            lf, height=10, wrap="word", state="disabled", font=("Courier", 11))
        self._console.pack(fill="both", expand=True, padx=4, pady=4)
        for tag, color in [
            ("cmd", "#b8860b"), ("resp", "#228b22"), ("evt", "#1e90ff"),
            ("info", "#555555"), ("warn", "#cc6600"), ("error", "#cc0000"),
        ]:
            self._console.tag_config(tag, foreground=color)
        ttk.Button(lf, text="Clear console", command=self._clear_console).pack(
            side="right", padx=4, pady=(0, 4))

    # ── Config ────────────────────────────────────────────────────────────────

    def _populate_config(self):
        brokers = self._cfg.get("brokers", [])
        ports   = [str(p) for p in self._cfg.get("ports", [])]
        devices = self._cfg.get("devices", [])
        self._broker_cb["values"] = brokers
        self._port_cb["values"]   = ports
        if brokers: self._broker_var.set(brokers[0])
        if ports:   self._port_var.set(ports[0])
        self._device_cb["values"] = [d["id"] for d in devices]
        if devices:
            self._device_var.set(devices[0]["id"])
            self._devtype_var.set(devices[0].get("dev_type", "ferp-com"))
            self._group_var.set(devices[0].get("group", "default"))

    def _on_device_selected(self, _=None):
        sel = self._device_var.get()
        for d in self._cfg.get("devices", []):
            if d["id"] == sel:
                self._devtype_var.set(d.get("dev_type", "ferp-com"))
                self._group_var.set(d.get("group", "default"))
                break

    def _add_broker(self):
        dlg = AddBrokerDialog(self)
        if not dlg.result:
            return
        broker, port = dlg.result
        if broker and broker not in self._cfg["brokers"]:
            self._cfg["brokers"].append(broker)
        if port and port not in self._cfg["ports"]:
            self._cfg["ports"].append(port)
        save_config(self._cfg)
        self._populate_config()
        self._broker_var.set(broker)
        self._port_var.set(str(port))

    def _add_device(self):
        dlg = AddDeviceDialog(self)
        if not dlg.result:
            return
        dev = dlg.result
        if not any(d["id"] == dev["id"] for d in self._cfg["devices"]):
            self._cfg["devices"].append(dev)
            save_config(self._cfg)
            self._populate_config()
        self._device_var.set(dev["id"])
        self._devtype_var.set(dev["dev_type"])
        self._group_var.set(dev["group"])

    # ── Connection ────────────────────────────────────────────────────────────

    def _get_conn_info(self):
        broker   = self._broker_var.get().strip()
        dev_type = self._devtype_var.get().strip()
        group    = self._group_var.get().strip()
        dev_id   = self._device_var.get().strip()
        try:
            port = int(self._port_var.get().strip())
        except ValueError:
            port = 1883
        return broker, port, dev_type, group, dev_id

    def _do_connect(self):
        broker, port, dev_type, group, dev_id = self._get_conn_info()
        if not all([broker, dev_id, dev_type, group]):
            messagebox.showerror("Missing fields",
                                  "Fill in Broker, Port, Device ID, Dev-type and Group.")
            return
        self._mqtt.connect(broker, port, build_topic_base(dev_type, group, dev_id))

    def _do_disconnect(self):
        self._mqtt.disconnect()

    def _on_status_change(self, connected: bool):
        self.after(0, self._update_status_ui, connected)

    def _update_status_ui(self, connected: bool):
        if connected:
            self._status_lbl.config(text="\u25cf Connected", foreground="green")
            self._connect_btn.config(state="disabled")
            self._disconnect_btn.config(state="normal")
        else:
            self._status_lbl.config(text="\u25cf Disconnected", foreground="red")
            self._connect_btn.config(state="normal")
            self._disconnect_btn.config(state="disabled")

    # ── Message selection ─────────────────────────────────────────────────────

    def _on_msg_selected(self, msg_name: str):
        self._cmd_panel.load_message(msg_name)

    # ── Console ───────────────────────────────────────────────────────────────

    def _append_log(self, text: str, tag: str = "info"):
        ts = time.strftime("%H:%M:%S")
        print(f"[{ts}] {text}", flush=True)
        self.after(0, self._insert_console, text, tag)

    def _insert_console(self, text: str, tag: str):
        ts = time.strftime("%H:%M:%S")
        self._console.config(state="normal")
        self._console.insert("end", f"[{ts}] {text}\n", tag)
        self._console.see("end")
        self._console.config(state="disabled")

    def _clear_console(self):
        self._console.config(state="normal")
        self._console.delete("1.0", "end")
        self._console.config(state="disabled")

    def _on_close(self):
        self._mqtt.disconnect()
        self.destroy()


if __name__ == "__main__":
    FerpMqttGui().mainloop()
