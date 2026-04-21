#!/usr/bin/env python3
"""
ferp-com Simulator UI
=====================
Connects to the ferp-com-simulator over a TCP socket (default port 9000)
and visualises hardware state in real time.

Usage:
    # Start simulator first:
    ./build/ferp-com-simulator --ui-port 9000

    # Then launch this UI:
    python3 tools/sim-ui/sim_ui.py --port 9000

Each line from the simulator is a JSON object:
    {"id": "MSG_WIFI_EVENT", "ts": 1234, "data": {...}}

This UI sends commands back:
    {"id": "SIM_BTN", "data": {"btn": "print1_short"}}
"""

import os
# Suppress the macOS system-Tk deprecation warning before tkinter is imported
os.environ.setdefault("TK_SILENCE_DEPRECATION", "1")

import argparse
import json
import queue
import random
import socket
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext
from datetime import datetime

from widgets.led_widget            import LedWidget
from widgets.nozzle_widget         import NozzleWidget
from widgets.log_widget            import LogWidget
from widgets.mqtt_widget           import MqttWidget
from widgets.ota_widget            import OtaWidget
from widgets.pool_widget           import PoolWidget
from widgets.config_widget         import ConfigWidget
from widgets.spiffs_widget         import SpiffsWidget
from widgets.sdcard_widget         import SdCardWidget
from widgets.message_inject_widget import MessageInjectWidget
from widgets.wifi_widget           import WifiWidget


# ─────────────────────────────────────────────────────────────────────────────
# TCP reader — runs in a background thread, puts parsed JSON into a queue
# ─────────────────────────────────────────────────────────────────────────────

class SimConnection:
    def __init__(self, host: str, port: int, rx_queue: queue.Queue):
        self._host     = host
        self._port     = port
        self._q        = rx_queue
        self._sock     = None
        self._running  = False
        self._send_lock = threading.Lock()

    def connect(self) -> bool:
        try:
            self._sock = socket.create_connection((self._host, self._port), timeout=5)
            self._sock.settimeout(None)
            self._running = True
            t = threading.Thread(target=self._reader, daemon=True)
            t.start()
            return True
        except OSError as e:
            self._q.put({"id": "_SIM_ERROR", "data": {"msg": str(e)}})
            return False

    def send(self, obj: dict):
        if self._sock is None:
            return
        line = json.dumps(obj) + "\n"
        with self._send_lock:
            try:
                self._sock.sendall(line.encode())
            except OSError:
                pass

    def _reader(self):
        buf = b""
        while self._running:
            try:
                chunk = self._sock.recv(4096)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.strip()
                    if line:
                        try:
                            obj = json.loads(line.decode())
                            self._q.put(obj)
                        except json.JSONDecodeError:
                            # Not JSON — treat as plain log line
                            self._q.put({"id": "_LOG", "data": {"text": line.decode()}})
            except OSError:
                break
        self._q.put({"id": "_SIM_DISCONNECTED", "data": {}})

    def close(self):
        self._running = False
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass


# ─────────────────────────────────────────────────────────────────────────────
# Main application window
# ─────────────────────────────────────────────────────────────────────────────

class App(tk.Tk):
    def __init__(self, host: str, port: int, demo: bool = False):
        super().__init__()
        self.title("ferp-com Simulator UI")
        self.configure(bg="#1e1e2e")
        self.resizable(True, True)
        self.geometry("1100x700")   # sensible initial size

        self._rx_queue = queue.Queue()
        self._conn     = SimConnection(host, port, self._rx_queue)
        self._tick_count = 0
        self._demo_mode  = demo
        self._demo_after = None

        self._build_ui()
        if demo:
            self._start_demo()
        else:
            self._connect(host, port)
        self._poll()

    # ── UI construction ──────────────────────────────────────────────────────

    def _build_ui(self):
        # ── Top status bar ────────────────────────────────────────────────────
        top = tk.Frame(self, bg="#11111b", pady=4)
        top.pack(fill=tk.X)

        self._status_label = tk.Label(
            top, text="⏳ connecting…",
            bg="#11111b", fg="#cdd6f4",
            font=("Menlo", 11))
        self._status_label.pack(side=tk.LEFT, padx=10)

        self._tick_label = tk.Label(
            top, text="tick: 0",
            bg="#11111b", fg="#a6e3a1",
            font=("Menlo", 11))
        self._tick_label.pack(side=tk.RIGHT, padx=10)

        # ── Main paned area ───────────────────────────────────────────────────
        main = tk.PanedWindow(self, orient=tk.HORIZONTAL,
                              bg="#1e1e2e", sashwidth=4)
        main.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

        # Left column — system indicators
        left = tk.Frame(main, bg="#1e1e2e", width=260)
        main.add(left, minsize=220)

        self._build_system_panel(left)
        self._build_button_panel(left)

        # Right column — log + tabs
        right = tk.Frame(main, bg="#1e1e2e")
        main.add(right, minsize=400)

        notebook = ttk.Notebook(right)
        notebook.pack(fill=tk.BOTH, expand=True)

        # Tab: Console log
        log_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(log_frame, text="Console")
        self._log = LogWidget(log_frame)
        self._log.pack(fill=tk.BOTH, expand=True)

        # Tab: Nozzles
        nozzle_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(nozzle_frame, text="Nozzles")
        self._nozzle_0 = NozzleWidget(nozzle_frame, label="Nozzle 1",
                                       nozzle_idx=0, send_fn=self._send_cmd)
        self._nozzle_0.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=4, pady=4)
        self._nozzle_1 = NozzleWidget(nozzle_frame, label="Nozzle 2",
                                       nozzle_idx=1, send_fn=self._send_cmd)
        self._nozzle_1.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=4, pady=4)

        # Tab: MQTT
        mqtt_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(mqtt_frame, text="MQTT")
        self._mqtt = MqttWidget(mqtt_frame, send_fn=self._send_cmd)
        self._mqtt.pack(fill=tk.BOTH, expand=True)

        # Tab: WiFi
        wifi_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(wifi_frame, text="WiFi")
        self._wifi_widget = WifiWidget(wifi_frame, send_fn=self._send_cmd)
        self._wifi_widget.pack(fill=tk.BOTH, expand=True)

        # Tab: OTA
        ota_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(ota_frame, text="OTA")
        self._ota = OtaWidget(ota_frame, send_fn=self._send_cmd)
        self._ota.pack(fill=tk.BOTH, expand=True)

        # Tab: Pool memory
        pool_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(pool_frame, text="Pool")
        self._pool = PoolWidget(pool_frame)
        self._pool.pack(fill=tk.BOTH, expand=True)

        # Tab: Device Config
        config_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(config_frame, text="Config")
        self._config = ConfigWidget(config_frame)
        self._config.pack(fill=tk.BOTH, expand=True)

        # Tab: SPIFFS Explorer
        spiffs_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(spiffs_frame, text="SPIFFS")
        self._spiffs = SpiffsWidget(spiffs_frame)
        self._spiffs.pack(fill=tk.BOTH, expand=True)

        # Tab: SD Card Explorer
        sdcard_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(sdcard_frame, text="SDCard")
        self._sdcard = SdCardWidget(sdcard_frame)
        self._sdcard.pack(fill=tk.BOTH, expand=True)

        # Tab: Message Injector
        msg_frame = tk.Frame(notebook, bg="#1e1e2e")
        notebook.add(msg_frame, text="Messages")

        # Resolve canonical paths from the repo root (two levels up from sim_ui.py)
        _sim_ui_dir = os.path.dirname(os.path.abspath(__file__))
        _src_dir    = os.path.join(_sim_ui_dir, "..", "..", "src")
        _msgs_dir   = os.path.join(_src_dir, "app-messages", "messages")
        _mods_json  = os.path.join(_src_dir, "app-modules", "modules.json")

        self._msg_inject = MessageInjectWidget(
            msg_frame,
            send_fn=self._send_cmd,
            messages_dir=_msgs_dir,
            modules_json=_mods_json,
        )
        self._msg_inject.pack(fill=tk.BOTH, expand=True)
    def _build_system_panel(self, parent):
        frame = tk.LabelFrame(
            parent, text="System", bg="#1e1e2e",
            fg="#cdd6f4", font=("Menlo", 10, "bold"))
        frame.pack(fill=tk.X, padx=4, pady=4)

        self._led_power    = LedWidget(frame, label="Power",    color_on="#a6e3a1")
        self._led_wifi     = LedWidget(frame, label="WiFi",     color_on="#89dceb")
        self._led_internet = LedWidget(frame, label="Internet", color_on="#89b4fa")
        self._led_cloud    = LedWidget(frame, label="Cloud",    color_on="#cba6f7")
        self._led_pump     = LedWidget(frame, label="Pump",     color_on="#f9e2af")
        self._led_led1     = LedWidget(frame, label="LED1",     color_on="#fab387")
        self._led_led2     = LedWidget(frame, label="LED2",     color_on="#fab387")
        self._led_buzzer   = LedWidget(frame, label="Buzzer",   color_on="#f9e2af")

        for w in (self._led_power, self._led_wifi,
                  self._led_internet, self._led_cloud, self._led_pump,
                  self._led_led1, self._led_led2, self._led_buzzer):
            w.pack(anchor=tk.W, padx=6, pady=2)

        # Start power LED on immediately
        self._led_power.set_on(True)

        # WiFi RSSI bar
        rssi_row = tk.Frame(frame, bg="#1e1e2e")
        rssi_row.pack(fill=tk.X, padx=6, pady=2)
        tk.Label(rssi_row, text="RSSI", bg="#1e1e2e", fg="#6c7086",
                 font=("Menlo", 9)).pack(side=tk.LEFT)
        self._rssi_bar = ttk.Progressbar(rssi_row, length=100, maximum=100)
        self._rssi_bar.pack(side=tk.LEFT, padx=4)
        self._rssi_label = tk.Label(
            rssi_row, text="—", bg="#1e1e2e", fg="#cdd6f4",
            font=("Menlo", 9))
        self._rssi_label.pack(side=tk.LEFT)

    def _build_button_panel(self, parent):
        frame = tk.LabelFrame(
            parent, text="Buttons", bg="#1e1e2e",
            fg="#cdd6f4", font=("Menlo", 10, "bold"))
        frame.pack(fill=tk.X, padx=4, pady=4)

        tk.Label(frame, text="Hold = long press · Click = short press",
                 bg="#1e1e2e", fg="#585b70",
                 font=("Menlo", 8)).pack(padx=6, pady=(2, 4))

        hw_buttons = [
            ("Default",  "default"),
            ("Print 1",  "print1"),
            ("Print 2",  "print2"),
        ]
        for label, btn_name in hw_buttons:
            btn = tk.Button(
                frame, text=label,
                bg="#313244", fg="#cdd6f4",
                activebackground="#89b4fa",
                font=("Menlo", 11, "bold"),
                relief=tk.FLAT, padx=8, pady=6)
            btn.pack(fill=tk.X, padx=6, pady=3)
            # Bind press and release — mirrors real hardware GPIO
            btn.bind("<ButtonPress-1>",
                     lambda e, n=btn_name, b=btn: self._btn_press(n, b))
            btn.bind("<ButtonRelease-1>",
                     lambda e, n=btn_name, b=btn: self._btn_release(n, b))

    # ── Connection ────────────────────────────────────────────────────────────

    def _connect(self, host: str, port: int):
        self._host = host
        self._port = port
        ok = self._conn.connect()
        if ok:
            self._stop_demo()
            self._set_status("✅ connected to {}:{}".format(host, port), "#a6e3a1")
        else:
            self._set_status("⏳ waiting for simulator on {}:{}  [DEMO mode]".format(host, port), "#f9e2af")
            self._start_demo()
            self.after(3000, self._retry_connect)

    def _retry_connect(self):
        self._conn = SimConnection(self._host, self._port, self._rx_queue)
        ok = self._conn.connect()
        if ok:
            self._stop_demo()
            self._set_status("✅ connected to {}:{}".format(self._host, self._port), "#a6e3a1")
        else:
            self.after(3000, self._retry_connect)

    def _start_demo(self):
        """Feed random demo data so the UI shows something without a simulator."""
        if self._demo_mode:
            self._set_status("🎭 DEMO mode — no simulator needed", "#cba6f7")
        self._pool.start_demo(interval_ms=2000)
        self._led_power.set_on(True)
        self._demo_tick_job = self.after(1000, self._demo_tick)

    def _stop_demo(self):
        self._pool.stop_demo()
        if hasattr(self, "_demo_tick_job") and self._demo_tick_job:
            self.after_cancel(self._demo_tick_job)
            self._demo_tick_job = None

    def _demo_tick(self):
        """Simulate a 1-second tick + random events while in demo/waiting mode."""
        self._tick_count += 1
        self._tick_label.config(text="tick: {}  [demo]".format(self._tick_count))

        # Fake log line every tick
        self._log.append_text(
            "[demo] tick {}  — no simulator connected".format(self._tick_count),
            color="#585b70")

        # Randomly toggle some LEDs
        if self._tick_count % 7 == 0:
            self._led_wifi.set_on(random.random() > 0.3)
            rssi = random.randint(-80, -30)
            pct  = max(0, min(100, rssi + 100))
            self._rssi_bar["value"] = pct
            self._rssi_label.config(text="{} dBm".format(rssi))
        if self._tick_count % 13 == 0:
            self._led_internet.set_on(random.random() > 0.4)
        if self._tick_count % 17 == 0:
            self._led_cloud.set_on(random.random() > 0.5)

        if not self._demo_mode:
            # Only keep ticking while waiting for real connection
            self._demo_tick_job = self.after(1000, self._demo_tick)
        else:
            self._demo_tick_job = self.after(1000, self._demo_tick)

    def _set_status(self, text: str, color: str = "#cdd6f4"):
        self._status_label.config(text=text, fg=color)

    # ── Event dispatch ────────────────────────────────────────────────────────

    def _poll(self):
        """Called every 50 ms on the Tk main thread to drain the RX queue."""
        try:
            while True:
                obj = self._rx_queue.get_nowait()
                self._dispatch(obj)
        except queue.Empty:
            pass
        self.after(50, self._poll)

    def _dispatch(self, obj: dict):
        msg_id = obj.get("id", "")
        data   = obj.get("data", {})
        ts     = obj.get("ts", 0)

        # Log everything except bulky pool snapshots and high-freq GPIO updates
        if msg_id not in ("SIM_POOL_STATUS", "SIM_GPIO_OUT"):
            self._log.append(obj)

        if msg_id == "_SIM_DISCONNECTED":
            self._set_status("🔌 simulator disconnected — reconnecting…  [DEMO mode]", "#f9e2af")
            self._led_power.set_on(False)
            self._start_demo()
            self.after(3000, self._retry_connect)

        elif msg_id == "_SIM_ERROR":
            self._set_status(f"❌ {data.get('msg')}", "#f38ba8")

        elif msg_id == "MSG_TICK_1000MS":
            self._tick_count += 1
            self._tick_label.config(text=f"tick: {self._tick_count}")

        elif msg_id == "MSG_WIFI_EVENT":
            self._handle_wifi_event(data)

        elif msg_id == "MSG_INTERNET_STATUS":
            connected = data.get("connected", False)
            self._led_internet.set_on(connected)

        elif msg_id == "MSG_NOZZLE_STATE":
            self._handle_nozzle_state(data)

        elif msg_id == "MSG_FUEL_PUMPED":
            self._handle_fuel_pumped(data)

        elif msg_id == "MSG_CLOUD_STATUS":
            self._handle_cloud_status(data)

        elif msg_id == "MSG_MQTT_EVENT":
            self._mqtt.on_event(data)

        elif msg_id == "MSG_MQTT_RX_MESSAGE":
            self._mqtt.on_rx(data)

        elif msg_id == "MSG_OTA_EVENT":
            self._ota.on_event(data)

        elif msg_id == "SIM_LED":
            self._handle_sim_led(data)

        elif msg_id == "SIM_GPIO_OUT":
            self._handle_sim_gpio_out(data)

        elif msg_id == "MSG_DEFAULT_BTN":
            status = data.get("status", "")
            self._log.append_text(f"🔘 DEFAULT BTN — {status}", color="#cba6f7")

        elif msg_id == "MSG_PRINTER_BTN":
            btn_id = data.get("button_id", "?")
            status = data.get("status", "")
            self._log.append_text(f"🖨  PRINT{btn_id} BTN — {status}", color="#89dceb")

        elif msg_id == "SIM_BUZZER":
            self._handle_buzzer(data)

        elif msg_id == "SIM_POOL_STATUS":
            self._pool.on_pool_status(data)

    def _handle_wifi_event(self, data: dict):
        event = data.get("event", "")
        if event in ("STA_CONNECTED", "GOT_IP"):
            self._led_wifi.set_on(True)
            rssi = data.get("rssi", -100)
            pct  = max(0, min(100, rssi + 100))
            self._rssi_bar["value"] = pct
            self._rssi_label.config(text=f"{rssi} dBm")
        elif event == "STA_DISCONNECTED":
            self._led_wifi.set_on(False)
            self._led_internet.set_on(False)
            self._rssi_bar["value"] = 0
            self._rssi_label.config(text="—")
        elif event == "STA_RSSI_CHANGED":
            rssi = data.get("rssi", -100)
            pct  = max(0, min(100, rssi + 100))
            self._rssi_bar["value"] = pct
            self._rssi_label.config(text=f"{rssi} dBm")

        # Forward to the dedicated WiFi tab widget
        self._wifi_widget.on_wifi_event(data)

    def _handle_nozzle_state(self, data: dict):
        idx   = data.get("idx", 0)
        state = data.get("state", "IDLE")
        w = self._nozzle_0 if idx == 0 else self._nozzle_1
        w.set_state(state)
        self._led_pump.set_on(state == "PUMPING")

    def _handle_fuel_pumped(self, data: dict):
        idx = data.get("idx", 0)
        w = self._nozzle_0 if idx == 0 else self._nozzle_1
        w.set_pumped(
            vol   = data.get("vol_lx1000", 0) / 1000.0,
            unit  = data.get("unit_pricex100", 0) / 100.0,
            total = data.get("total_pricex100", 0) / 100.0,
        )
        # Brief flash on pump LED
        self._led_pump.flash(duration_ms=800, color="#f9e2af")

    def _handle_cloud_status(self, data: dict):
        event = data.get("event", "")
        ok = event in ("REGISTERED", "PUMPED_SUCCESS", "HB_SENT")
        self._led_cloud.set_on(ok)

    def _handle_sim_led(self, data: dict):
        led   = data.get("led", "")
        state = data.get("state", "off") == "on"
        mapping = {
            "power":    self._led_power,
            "wifi":     self._led_wifi,
            "internet": self._led_internet,
            "cloud":    self._led_cloud,
            "pump":     self._led_pump,
        }
        if led in mapping:
            mapping[led].set_on(state)

    def _handle_buzzer(self, data: dict):
        pattern = data.get("pattern", "beep")
        self._log.append_text(f"🔔 BUZZER: {pattern}", color="#f9e2af")

    def _handle_sim_gpio_out(self, data: dict):
        """Handle SIM_GPIO_OUT — physical GPIO output level changes from pal_mac_gpio."""
        pin   = data.get("pin", -1)
        level = data.get("level", 0) != 0
        name  = data.get("name", "")
        # Map by name (preferred) or pin number
        if name == "LED1" or pin == 5:
            self._led_led1.set_on(level)
        elif name == "LED2" or pin == 4:
            self._led_led2.set_on(level)
        elif name == "BUZZER" or pin == 26:
            self._led_buzzer.set_on(level)
            if level:
                # Play a short system beep — non-blocking, macOS afplay
                subprocess.Popen(
                    ["afplay", "-v", "0.4",
                     "/System/Library/Sounds/Tink.aiff"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )

    # ── Outbound commands ─────────────────────────────────────────────────────

    def _btn_press(self, btn_name: str, widget: tk.Button):
        """Mouse-down on a hardware button — send press, highlight button."""
        widget.config(bg="#89b4fa", fg="#1e1e2e")
        self._send_cmd({"id": "SIM_BTN", "data": {"btn": btn_name, "action": "press"}})

    def _btn_release(self, btn_name: str, widget: tk.Button):
        """Mouse-up on a hardware button — send release, restore button colour."""
        widget.config(bg="#313244", fg="#cdd6f4")
        self._send_cmd({"id": "SIM_BTN", "data": {"btn": btn_name, "action": "release"}})

    def _send_cmd(self, obj: dict):
        self._conn.send(obj)
        self._log.append_text(f"→ {json.dumps(obj)}", color="#6c7086")


# ─────────────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ferp-com Simulator UI")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    args = parser.parse_args()

    app = App(args.host, args.port)
    app.mainloop()


if __name__ == "__main__":
    main()
