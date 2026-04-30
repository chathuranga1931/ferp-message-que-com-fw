"""
ferp_mqtt_gui.py — GUI front-end for the FERP MQTT command/listen tool.

Config file: ferp_mqtt_config.json (created next to this script on first run).
The first entry in every list is used as the default selection.

Layout
------
  Row 0  │ Broker ▾          Port ▾      [+Add]
  Row 1  │ Device ID ▾        Dev-type    Group      [+Add]
  Row 2  │ [Connect]  [Disconnect]   Status label
  ───────┼──────────────────────────────────────────────
  Row 3  │ Command ▾     [Fill fields ↓]   [Send]
  Row 4  │ Dynamic payload fields (key=value entries)
  ───────┼──────────────────────────────────────────────
  Row 5  │ Log / event feed (scrollable text area)
  Row 6  │ [Clear log]
"""

import json
import os
import random
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, simpledialog

# ---------------------------------------------------------------------------
# Optional paho import — guide user if missing
# ---------------------------------------------------------------------------
try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("ERROR: paho-mqtt not installed.\n"
          "Run:  pip install paho-mqtt   (or  .venv/bin/pip install paho-mqtt)\n",
          file=sys.stderr)
    sys.exit(1)

from messages.msg_defs import CMD_MSGS, RESP_MSGS, ALL_MSGS

# ---------------------------------------------------------------------------
# Config helpers
# ---------------------------------------------------------------------------

CONFIG_FILE = os.path.join(os.path.dirname(__file__), "ferp_mqtt_config.json")

DEFAULT_CONFIG = {
    "brokers": ["broker.emqx.io", "localhost"],
    "ports":   [1883, 8883],
    "devices": [
        # Matches ferp-com-simulator default config:
        #   device_uuid = "00000000-0000-0000-0000-000000000000"
        #   device_group = "default"
        #   dev_type is always "ferp-com" (hardcoded in module_cloud + ModuleMqtt)
        #   topic: ferp/ferp-com/default/00000000000000000000000000000000/cmd|resp|evt
        {"id": "00000000-0000-0000-0000-000000000000", "dev_type": "ferp-com", "group": "default"}
    ]
}


def load_config() -> dict:
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r") as f:
                cfg = json.load(f)
            # Merge any missing top-level keys from DEFAULT_CONFIG
            for k, v in DEFAULT_CONFIG.items():
                cfg.setdefault(k, v)
            return cfg
        except (json.JSONDecodeError, OSError):
            pass
    return dict(DEFAULT_CONFIG)


def save_config(cfg: dict):
    with open(CONFIG_FILE, "w") as f:
        json.dump(cfg, f, indent=2)


# ---------------------------------------------------------------------------
# Topic helpers (mirrors ferp_mqtt_tool.py)
# ---------------------------------------------------------------------------

def build_topic_base(dev_type: str, group: str, device_id: str) -> str:
    safe_id = device_id.replace(":", "").replace("-", "").lower()
    return f"ferp/{dev_type}/{group}/{safe_id}"


def cmd_topic(base: str) -> str:  return f"{base}/cmd"
def resp_topic(base: str) -> str: return f"{base}/resp"
def evt_topic(base: str) -> str:  return f"{base}/evt"


# ---------------------------------------------------------------------------
# MQTT client wrapper
# ---------------------------------------------------------------------------

class MqttClient:
    def __init__(self, on_log, on_status_change):
        self._on_log = on_log           # callable(str, tag)
        self._on_status = on_status_change  # callable(bool)
        self._client: mqtt.Client | None = None
        self._base_topic = ""
        self._pending_seq: int | None = None
        self._connected = False

    def connect(self, broker: str, port: int, base_topic: str):
        if self._client:
            self._client.disconnect()
            self._client.loop_stop()

        self._base_topic = base_topic
        client_id = f"ferp-gui-{random.randint(1000, 9999)}"
        self._client = mqtt.Client(client_id=client_id)
        self._client.on_connect    = self._handle_connect
        self._client.on_disconnect = self._handle_disconnect
        self._client.on_message    = self._handle_message

        try:
            self._client.connect(broker, port, keepalive=60)
            self._client.loop_start()
        except Exception as exc:
            self._on_log(f"[error] Could not connect: {exc}", "error")
            self._on_status(False)

    def disconnect(self):
        if self._client:
            self._client.disconnect()
            self._client.loop_stop()
            self._client = None
        self._connected = False
        self._on_status(False)

    def send(self, msg_name: str, data: dict):
        if not self._client or not self._connected:
            self._on_log("[error] Not connected", "error")
            return
        seq = random.randint(1, 0xFFFF_FFFF)
        self._pending_seq = seq
        payload = {"seq": seq, "msg": msg_name, "data": data}
        payload_str = json.dumps(payload)
        self._on_log(f"→ [cmd]  {payload_str}", "cmd")
        self._client.publish(cmd_topic(self._base_topic), payload_str, qos=1)

    @property
    def is_connected(self) -> bool:
        return self._connected

    # ── callbacks (called from paho thread) ─────────────────────────────────

    def _handle_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self._connected = True
            self._on_status(True)
            self._on_log(f"[info]  Connected to broker. Base topic: {self._base_topic}", "info")
            # Unsubscribe first to remove any stale entry from paho's internal
            # dispatch list (accumulates if on_connect fires more than once,
            # e.g. after an auto-reconnect), which would cause duplicate on_message calls.
            client.unsubscribe([resp_topic(self._base_topic), evt_topic(self._base_topic)])
            client.subscribe(resp_topic(self._base_topic), qos=1)
            client.subscribe(evt_topic(self._base_topic), qos=0)
            self._on_log(f"[info]  Subscribed to resp + evt topics", "info")
        else:
            self._on_log(f"[error] Connection refused (rc={rc})", "error")
            self._on_status(False)

    def _handle_disconnect(self, client, userdata, rc):
        self._connected = False
        self._on_status(False)
        if rc != 0:
            self._on_log(f"[warn]  Unexpected disconnect (rc={rc})", "warn")
        else:
            self._on_log("[info]  Disconnected", "info")

    def _handle_message(self, client, userdata, message):
        try:
            payload = json.loads(message.payload.decode("utf-8"))
        except Exception as exc:
            self._on_log(f"[warn]  Bad payload on {message.topic}: {exc}", "warn")
            return

        suffix = message.topic.split("/")[-1]
        tag = "evt" if suffix == "evt" else "resp"
        self._on_log(f"← [{suffix}]  {json.dumps(payload, indent=2)}", tag)


# ---------------------------------------------------------------------------
# "Add" dialogs
# ---------------------------------------------------------------------------

class AddBrokerDialog(simpledialog.Dialog):
    def body(self, master):
        self.title("Add broker")
        tk.Label(master, text="IP / hostname:").grid(row=0, column=0, sticky="w")
        self.broker_var = tk.StringVar()
        tk.Entry(master, textvariable=self.broker_var, width=30).grid(row=0, column=1, padx=4)
        tk.Label(master, text="Port:").grid(row=1, column=0, sticky="w")
        self.port_var = tk.StringVar(value="1883")
        tk.Entry(master, textvariable=self.port_var, width=10).grid(row=1, column=1, sticky="w", padx=4)
        return None

    def apply(self):
        try:
            self.result = (self.broker_var.get().strip(), int(self.port_var.get().strip()))
        except ValueError:
            self.result = None


class AddDeviceDialog(simpledialog.Dialog):
    def body(self, master):
        self.title("Add device")
        labels = ["Device ID:", "Dev-type:", "Group:"]
        self.vars = []
        defaults = ["AA:BB:CC:DD:EE:FF", "ferp-fuel", "default"]
        for i, (lbl, dflt) in enumerate(zip(labels, defaults)):
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
# Main GUI window
# ---------------------------------------------------------------------------

class FerpMqttGui(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("FERP MQTT Tool")
        self.minsize(780, 600)
        self.resizable(True, True)

        self._cfg = load_config()
        self._mqtt = MqttClient(
            on_log=self._append_log,
            on_status_change=self._on_connection_change,
        )
        self._payload_widgets: list[tuple[tk.StringVar, tk.StringVar]] = []

        self._build_ui()
        self._populate_from_config()
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ── UI construction ──────────────────────────────────────────────────────

    def _build_ui(self):
        pad = {"padx": 6, "pady": 4}

        # ── Top frame: connection settings ──────────────────────────────────
        conn_frame = ttk.LabelFrame(self, text="Connection")
        conn_frame.pack(fill="x", padx=8, pady=(8, 2))

        # Row 0: broker + port
        ttk.Label(conn_frame, text="Broker:").grid(row=0, column=0, sticky="w", **pad)
        self._broker_var = tk.StringVar()
        self._broker_cb = ttk.Combobox(conn_frame, textvariable=self._broker_var, width=24)
        self._broker_cb.grid(row=0, column=1, sticky="w", **pad)

        ttk.Label(conn_frame, text="Port:").grid(row=0, column=2, sticky="w", **pad)
        self._port_var = tk.StringVar()
        self._port_cb = ttk.Combobox(conn_frame, textvariable=self._port_var, width=8)
        self._port_cb.grid(row=0, column=3, sticky="w", **pad)

        ttk.Button(conn_frame, text="+ Add broker", command=self._add_broker).grid(
            row=0, column=4, sticky="w", **pad)

        # Row 1: device
        ttk.Label(conn_frame, text="Device ID:").grid(row=1, column=0, sticky="w", **pad)
        self._device_var = tk.StringVar()
        self._device_cb = ttk.Combobox(conn_frame, textvariable=self._device_var, width=24)
        self._device_cb.grid(row=1, column=1, sticky="w", **pad)
        self._device_cb.bind("<<ComboboxSelected>>", self._on_device_selected)

        ttk.Label(conn_frame, text="Dev-type:").grid(row=1, column=2, sticky="w", **pad)
        self._devtype_var = tk.StringVar()
        ttk.Entry(conn_frame, textvariable=self._devtype_var, width=14).grid(
            row=1, column=3, sticky="w", **pad)

        ttk.Label(conn_frame, text="Group:").grid(row=1, column=4, sticky="w", **pad)
        self._group_var = tk.StringVar()
        ttk.Entry(conn_frame, textvariable=self._group_var, width=14).grid(
            row=1, column=5, sticky="w", **pad)

        ttk.Button(conn_frame, text="+ Add device", command=self._add_device).grid(
            row=1, column=6, sticky="w", **pad)

        # Row 2: connect / disconnect / status
        btn_frame = ttk.Frame(conn_frame)
        btn_frame.grid(row=2, column=0, columnspan=7, sticky="w", **pad)
        self._connect_btn = ttk.Button(btn_frame, text="Connect", command=self._do_connect)
        self._connect_btn.pack(side="left", padx=(0, 6))
        self._disconnect_btn = ttk.Button(btn_frame, text="Disconnect",
                                          command=self._do_disconnect, state="disabled")
        self._disconnect_btn.pack(side="left", padx=(0, 12))
        self._status_lbl = ttk.Label(btn_frame, text="● Disconnected", foreground="red")
        self._status_lbl.pack(side="left")

        # ── Middle frame: command composer ───────────────────────────────────
        cmd_frame = ttk.LabelFrame(self, text="Command")
        cmd_frame.pack(fill="x", padx=8, pady=2)

        ttk.Label(cmd_frame, text="Message:").grid(row=0, column=0, sticky="w", **pad)
        self._cmd_var = tk.StringVar()
        self._cmd_cb = ttk.Combobox(cmd_frame, textvariable=self._cmd_var,
                                    values=sorted(CMD_MSGS.keys()), width=28, state="readonly")
        self._cmd_cb.grid(row=0, column=1, sticky="w", **pad)
        self._cmd_cb.bind("<<ComboboxSelected>>", self._on_cmd_selected)

        ttk.Button(cmd_frame, text="Fill fields ↓", command=self._fill_fields).grid(
            row=0, column=2, **pad)
        self._send_btn = ttk.Button(cmd_frame, text="Send", command=self._do_send,
                                    state="disabled")
        self._send_btn.grid(row=0, column=3, **pad)

        # Dynamic payload area
        self._payload_frame = ttk.Frame(cmd_frame)
        self._payload_frame.grid(row=1, column=0, columnspan=6, sticky="ew", padx=6, pady=2)

        # ── Bottom frame: log ─────────────────────────────────────────────────
        log_frame = ttk.LabelFrame(self, text="Log")
        log_frame.pack(fill="both", expand=True, padx=8, pady=(2, 8))

        self._log = scrolledtext.ScrolledText(log_frame, height=18, wrap="word",
                                              state="disabled", font=("Courier", 11))
        self._log.pack(fill="both", expand=True, padx=4, pady=4)

        # Tags for colour
        self._log.tag_config("cmd",   foreground="#b8860b")
        self._log.tag_config("resp",  foreground="#228b22")
        self._log.tag_config("evt",   foreground="#1e90ff")
        self._log.tag_config("info",  foreground="#555555")
        self._log.tag_config("warn",  foreground="#cc6600")
        self._log.tag_config("error", foreground="#cc0000")

        btn_bar = ttk.Frame(log_frame)
        btn_bar.pack(fill="x", padx=4, pady=(0, 4))
        ttk.Button(btn_bar, text="Clear log", command=self._clear_log).pack(side="right")

    # ── Populate dropdowns from config ───────────────────────────────────────

    def _populate_from_config(self):
        cfg = self._cfg

        brokers = cfg.get("brokers", [])
        ports   = [str(p) for p in cfg.get("ports", [])]
        devices = cfg.get("devices", [])

        self._broker_cb["values"] = brokers
        self._port_cb["values"]   = ports

        if brokers:
            self._broker_var.set(brokers[0])
        if ports:
            self._port_var.set(ports[0])

        device_ids = [d["id"] for d in devices]
        self._device_cb["values"] = device_ids
        if devices:
            self._device_var.set(devices[0]["id"])
            self._devtype_var.set(devices[0].get("dev_type", "ferp-fuel"))
            self._group_var.set(devices[0].get("group", "default"))

    def _on_device_selected(self, _event=None):
        selected_id = self._device_var.get()
        for d in self._cfg.get("devices", []):
            if d["id"] == selected_id:
                self._devtype_var.set(d.get("dev_type", "ferp-fuel"))
                self._group_var.set(d.get("group", "default"))
                break

    # ── Add broker/device dialogs ─────────────────────────────────────────────

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
        self._populate_from_config()
        self._broker_var.set(broker)
        self._port_var.set(str(port))

    def _add_device(self):
        dlg = AddDeviceDialog(self)
        if not dlg.result:
            return
        dev = dlg.result
        # Avoid duplicates
        if not any(d["id"] == dev["id"] for d in self._cfg["devices"]):
            self._cfg["devices"].append(dev)
            save_config(self._cfg)
            self._populate_from_config()
        self._device_var.set(dev["id"])
        self._devtype_var.set(dev["dev_type"])
        self._group_var.set(dev["group"])

    # ── Connect / disconnect ──────────────────────────────────────────────────

    def _do_connect(self):
        broker   = self._broker_var.get().strip()
        dev_type = self._devtype_var.get().strip()
        group    = self._group_var.get().strip()
        dev_id   = self._device_var.get().strip()

        try:
            port = int(self._port_var.get().strip())
        except ValueError:
            messagebox.showerror("Bad port", "Port must be an integer.")
            return

        if not broker or not dev_id or not dev_type or not group:
            messagebox.showerror("Missing fields",
                                 "Fill in Broker, Port, Device ID, Dev-type and Group.")
            return

        base = build_topic_base(dev_type, group, dev_id)
        self._mqtt.connect(broker, port, base)

    def _do_disconnect(self):
        self._mqtt.disconnect()

    def _on_connection_change(self, connected: bool):
        # Called from paho thread — schedule UI update on main thread
        self.after(0, self._update_connection_ui, connected)

    def _update_connection_ui(self, connected: bool):
        if connected:
            self._status_lbl.config(text="● Connected", foreground="green")
            self._connect_btn.config(state="disabled")
            self._disconnect_btn.config(state="normal")
            self._send_btn.config(state="normal")
        else:
            self._status_lbl.config(text="● Disconnected", foreground="red")
            self._connect_btn.config(state="normal")
            self._disconnect_btn.config(state="disabled")
            self._send_btn.config(state="disabled")

    # ── Command composer ──────────────────────────────────────────────────────

    def _on_cmd_selected(self, _event=None):
        self._fill_fields()

    def _fill_fields(self):
        # Clear old widgets
        for w in self._payload_frame.winfo_children():
            w.destroy()
        self._payload_widgets.clear()

        msg_name = self._cmd_var.get()
        if msg_name not in CMD_MSGS:
            return

        fields = CMD_MSGS[msg_name].get("fields", {})
        if not fields:
            ttk.Label(self._payload_frame, text="(no payload fields)",
                      foreground="gray").grid(row=0, column=0, padx=4)
            return

        for col, (field, ftype) in enumerate(fields.items()):
            ttk.Label(self._payload_frame, text=f"{field}:").grid(
                row=0, column=col * 2, sticky="w", padx=(8, 2))
            v = tk.StringVar()
            ttk.Entry(self._payload_frame, textvariable=v, width=16).grid(
                row=0, column=col * 2 + 1, sticky="w", padx=(0, 8))
            self._payload_widgets.append((field, v, ftype))

    def _do_send(self):
        msg_name = self._cmd_var.get()
        if not msg_name:
            messagebox.showerror("No message", "Select a command message first.")
            return

        fields = CMD_MSGS.get(msg_name, {}).get("fields", {})
        data: dict = {}

        for field, var, ftype in self._payload_widgets:
            raw = var.get().strip()
            if not raw:
                messagebox.showerror("Missing field", f"Field '{field}' is empty.")
                return
            try:
                if ftype == int:
                    data[field] = int(raw)
                elif ftype == float:
                    data[field] = float(raw)
                elif ftype == bool:
                    data[field] = raw.lower() in ("1", "true", "yes")
                else:
                    data[field] = raw
            except ValueError:
                messagebox.showerror("Bad value",
                                     f"Field '{field}' expects {ftype.__name__}, got: {raw!r}")
                return

        self._mqtt.send(msg_name, data)

    # ── Log helpers ───────────────────────────────────────────────────────────

    def _append_log(self, text: str, tag: str = "info"):
        # Mirror every event to the console (stdout) so the terminal shows live traffic
        ts = time.strftime("%H:%M:%S")
        print(f"[{ts}] {text}", flush=True)
        # May be called from any thread — schedule GUI insert on main thread
        self.after(0, self._insert_log, text, tag)

    def _insert_log(self, text: str, tag: str):
        ts = time.strftime("%H:%M:%S")
        self._log.config(state="normal")
        self._log.insert("end", f"[{ts}] {text}\n", tag)
        self._log.see("end")
        self._log.config(state="disabled")

    def _clear_log(self):
        self._log.config(state="normal")
        self._log.delete("1.0", "end")
        self._log.config(state="disabled")

    def _on_close(self):
        self._mqtt.disconnect()
        self.destroy()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    app = FerpMqttGui()
    app.mainloop()
