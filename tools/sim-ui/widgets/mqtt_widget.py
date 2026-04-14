"""
MqttWidget — shows MQTT connection state, received messages and lets the
user inject inbound MQTT messages into the simulator.
"""

import json
import tkinter as tk
from tkinter import scrolledtext
from datetime import datetime

_BG = "#1e1e2e"
_FG = "#cdd6f4"


class MqttWidget(tk.Frame):
    def __init__(self, parent, send_fn, **kwargs):
        super().__init__(parent, bg=_BG, **kwargs)
        self._send = send_fn

        # ── Connection state bar ──────────────────────────────────────────────
        top = tk.Frame(self, bg="#11111b")
        top.pack(fill=tk.X)

        self._conn_label = tk.Label(
            top, text="● DISCONNECTED",
            bg="#11111b", fg="#f38ba8",
            font=("Menlo", 10, "bold"), pady=4, padx=8)
        self._conn_label.pack(side=tk.LEFT)

        # ── Received messages log ─────────────────────────────────────────────
        tk.Label(self, text="Received messages:",
                 bg=_BG, fg="#6c7086",
                 font=("Menlo", 9)).pack(anchor=tk.W, padx=6, pady=(6, 0))

        self._rx_log = scrolledtext.ScrolledText(
            self, bg="#11111b", fg=_FG,
            font=("Menlo", 9), height=10,
            state=tk.DISABLED, relief=tk.FLAT)
        self._rx_log.pack(fill=tk.BOTH, expand=True, padx=6, pady=4)

        # ── Inject panel ──────────────────────────────────────────────────────
        inj = tk.LabelFrame(self, text="Inject RX message",
                            bg=_BG, fg=_FG,
                            font=("Menlo", 9, "bold"))
        inj.pack(fill=tk.X, padx=6, pady=4)

        row1 = tk.Frame(inj, bg=_BG)
        row1.pack(fill=tk.X, padx=4, pady=2)
        tk.Label(row1, text="Topic:", bg=_BG, fg="#6c7086",
                 font=("Menlo", 9), width=8, anchor=tk.W).pack(side=tk.LEFT)
        self._topic_var = tk.StringVar(value="ferp/cmd/ota")
        tk.Entry(row1, textvariable=self._topic_var,
                 bg="#313244", fg=_FG, insertbackground=_FG,
                 font=("Menlo", 9), relief=tk.FLAT).pack(side=tk.LEFT, fill=tk.X, expand=True)

        row2 = tk.Frame(inj, bg=_BG)
        row2.pack(fill=tk.X, padx=4, pady=2)
        tk.Label(row2, text="Payload:", bg=_BG, fg="#6c7086",
                 font=("Menlo", 9), width=8, anchor=tk.W).pack(side=tk.LEFT)
        self._payload_var = tk.StringVar(value='{"cmd":"check"}')
        tk.Entry(row2, textvariable=self._payload_var,
                 bg="#313244", fg=_FG, insertbackground=_FG,
                 font=("Menlo", 9), relief=tk.FLAT).pack(side=tk.LEFT, fill=tk.X, expand=True)

        tk.Button(inj, text="Inject →",
                  bg="#313244", fg=_FG,
                  activebackground="#45475a",
                  font=("Menlo", 9), relief=tk.FLAT,
                  command=self._inject).pack(padx=4, pady=4)

    # ── Public API ────────────────────────────────────────────────────────────

    def on_event(self, data: dict):
        event = data.get("event", "")
        if event == "CONNECTED":
            self._conn_label.config(text="● CONNECTED", fg="#a6e3a1")
        elif event == "DISCONNECTED":
            self._conn_label.config(text="● DISCONNECTED", fg="#f38ba8")

    def on_rx(self, data: dict):
        topic   = data.get("topic", "?")
        payload = data.get("payload", "")
        now     = datetime.now().strftime("%H:%M:%S")
        line    = f"[{now}] {topic:30s} {payload}\n"
        self._rx_log.config(state=tk.NORMAL)
        self._rx_log.insert(tk.END, line)
        self._rx_log.see(tk.END)
        self._rx_log.config(state=tk.DISABLED)

    # ── Private ───────────────────────────────────────────────────────────────

    def _inject(self):
        topic   = self._topic_var.get().strip()
        payload = self._payload_var.get().strip()
        if not topic:
            return
        self._send({
            "id": "SIM_MQTT_INJECT",
            "data": {"topic": topic, "payload": payload}
        })
