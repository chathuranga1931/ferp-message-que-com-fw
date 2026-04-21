"""
WifiWidget — displays WiFi connection state and allows simulating events.

Shows:
  - Connection state badge (DISCONNECTED / CONNECTING / CONNECTED)
  - SSID, IP address, MAC address, RSSI bar
  - Quick-inject buttons: Inject GOT_IP, Inject DISCONNECT
"""

import tkinter as tk
from tkinter import ttk


class WifiWidget(tk.Frame):

    _STATES = {
        "DISCONNECTED": "#f38ba8",
        "CONNECTING":   "#f9e2af",
        "CONNECTED":    "#a6e3a1",
    }

    def __init__(self, parent, send_fn=None, **kwargs):
        super().__init__(parent, bg="#1e1e2e", **kwargs)
        self._send = send_fn
        self._state = "DISCONNECTED"
        self._build_ui()

    # ── Build ─────────────────────────────────────────────────────────────────

    def _build_ui(self):
        # ── Status banner ─────────────────────────────────────────────────────
        banner_frame = tk.Frame(self, bg="#1e1e2e")
        banner_frame.pack(fill=tk.X, padx=8, pady=(8, 4))

        tk.Label(banner_frame, text="WiFi Status",
                 bg="#1e1e2e", fg="#cdd6f4",
                 font=("Menlo", 11, "bold")).pack(side=tk.LEFT)

        self._state_label = tk.Label(
            banner_frame, text="DISCONNECTED",
            bg="#f38ba8", fg="#1e1e2e",
            font=("Menlo", 10, "bold"), padx=6, pady=2)
        self._state_label.pack(side=tk.LEFT, padx=8)

        # ── Info grid ─────────────────────────────────────────────────────────
        info = tk.LabelFrame(self, text="Connection Info",
                             bg="#1e1e2e", fg="#cdd6f4",
                             font=("Menlo", 10, "bold"))
        info.pack(fill=tk.X, padx=8, pady=4)

        def row(label, default="—"):
            r = tk.Frame(info, bg="#1e1e2e")
            r.pack(fill=tk.X, padx=6, pady=2)
            tk.Label(r, text=f"{label}:", bg="#1e1e2e", fg="#6c7086",
                     font=("Menlo", 9), width=8, anchor=tk.W).pack(side=tk.LEFT)
            val = tk.Label(r, text=default, bg="#1e1e2e", fg="#cdd6f4",
                           font=("Menlo", 9), anchor=tk.W)
            val.pack(side=tk.LEFT)
            return val

        self._lbl_ssid  = row("SSID")
        self._lbl_ip    = row("IP")
        self._lbl_mac   = row("MAC")

        # RSSI row with progress bar
        rssi_row = tk.Frame(info, bg="#1e1e2e")
        rssi_row.pack(fill=tk.X, padx=6, pady=2)
        tk.Label(rssi_row, text="RSSI:", bg="#1e1e2e", fg="#6c7086",
                 font=("Menlo", 9), width=8, anchor=tk.W).pack(side=tk.LEFT)
        self._rssi_bar = ttk.Progressbar(rssi_row, length=120, maximum=100)
        self._rssi_bar.pack(side=tk.LEFT, padx=4)
        self._lbl_rssi = tk.Label(rssi_row, text="—", bg="#1e1e2e", fg="#cdd6f4",
                                   font=("Menlo", 9))
        self._lbl_rssi.pack(side=tk.LEFT)

        # ── Inject buttons ────────────────────────────────────────────────────
        btn_frame = tk.LabelFrame(self, text="Inject Event",
                                  bg="#1e1e2e", fg="#cdd6f4",
                                  font=("Menlo", 10, "bold"))
        btn_frame.pack(fill=tk.X, padx=8, pady=4)

        btns = [
            ("💡 Inject GOT_IP",       self._inject_got_ip),
            ("🔌 Inject DISCONNECT",   self._inject_disconnect),
            ("📶 Inject RSSI Changed", self._inject_rssi),
        ]
        for label, cmd in btns:
            tk.Button(btn_frame, text=label,
                      bg="#313244", fg="#cdd6f4",
                      activebackground="#89b4fa",
                      font=("Menlo", 10), relief=tk.FLAT,
                      padx=6, pady=4, command=cmd
                      ).pack(fill=tk.X, padx=6, pady=3)

        # ── SSID/Password entry for custom inject ─────────────────────────────
        cust_frame = tk.LabelFrame(self, text="Custom Credentials",
                                   bg="#1e1e2e", fg="#cdd6f4",
                                   font=("Menlo", 10, "bold"))
        cust_frame.pack(fill=tk.X, padx=8, pady=4)

        def entry_row(label, default=""):
            r = tk.Frame(cust_frame, bg="#1e1e2e")
            r.pack(fill=tk.X, padx=6, pady=2)
            tk.Label(r, text=f"{label}:", bg="#1e1e2e", fg="#6c7086",
                     font=("Menlo", 9), width=8, anchor=tk.W).pack(side=tk.LEFT)
            e = tk.Entry(r, bg="#313244", fg="#cdd6f4",
                         insertbackground="#cdd6f4",
                         font=("Menlo", 9), width=24)
            e.insert(0, default)
            e.pack(side=tk.LEFT, padx=4)
            return e

        self._entry_ssid = entry_row("SSID", "MyNetwork")
        self._entry_ip   = entry_row("IP",   "192.168.1.99")

    # ── Public API (called from sim_ui._dispatch) ──────────────────────────────

    def on_wifi_event(self, data: dict):
        """Update widget from a MSG_WIFI_EVENT payload dict."""
        event = data.get("event", "")
        if event == "STA_CONNECTED":
            self._set_state("CONNECTING")
            ssid = data.get("ssid", "")
            if ssid:
                self._lbl_ssid.config(text=ssid)
        elif event == "GOT_IP":
            self._set_state("CONNECTED")
            self._lbl_ssid.config(text=data.get("ssid", "—"))
            self._lbl_ip.config(text=data.get("ip", "—"))
            self._lbl_mac.config(text=data.get("mac", "—"))
            self._update_rssi(data.get("rssi", -100))
        elif event == "STA_DISCONNECTED":
            self._set_state("DISCONNECTED")
            self._lbl_ip.config(text="—")
            self._lbl_mac.config(text="—")
            self._rssi_bar["value"] = 0
            self._lbl_rssi.config(text="—")
        elif event == "STA_RSSI_CHANGED":
            self._update_rssi(data.get("rssi", -100))

    # ── Private helpers ───────────────────────────────────────────────────────

    def _set_state(self, state: str):
        self._state = state
        color = self._STATES.get(state, "#6c7086")
        self._state_label.config(text=state, bg=color)

    def _update_rssi(self, rssi: int):
        pct = max(0, min(100, rssi + 100))
        self._rssi_bar["value"] = pct
        self._lbl_rssi.config(text=f"{rssi} dBm")

    # ── Inject actions ────────────────────────────────────────────────────────

    def _inject_got_ip(self):
        if not self._send:
            return
        ssid = self._entry_ssid.get().strip() or "MyNetwork"
        ip   = self._entry_ip.get().strip()  or "192.168.1.99"
        payload = {
            "event":       "GOT_IP",
            "rssi":        -55,
            "ip_address":  ip,
            "ssid":        ssid,
            "mac_address": "AA:BB:CC:DD:EE:FF",
        }
        self._send({
            "id":   "SIM_MSG_INJECT",
            "data": {
                "msg_id":    "0x0A00",   # MSG_ID_WIFI_EVENT
                "src":       20,          # ModuleSimBridge as sender
                "dst":       0,
                "payload":   payload,
            }
        })

    def _inject_disconnect(self):
        if not self._send:
            return
        payload = {
            "event":       "STA_DISCONNECTED",
            "rssi":        -100,
            "ip_address":  "",
            "ssid":        "",
            "mac_address": "",
        }
        self._send({
            "id":   "SIM_MSG_INJECT",
            "data": {
                "msg_id":    "0x0A00",
                "src":       20,
                "dst":       0,
                "payload":   payload,
            }
        })

    def _inject_rssi(self):
        if not self._send:
            return
        import random
        rssi = random.randint(-85, -40)
        payload = {
            "event":       "STA_RSSI_CHANGED",
            "rssi":        rssi,
            "ip_address":  "",
            "ssid":        "",
            "mac_address": "",
        }
        self._send({
            "id":   "SIM_MSG_INJECT",
            "data": {
                "msg_id":    "0x0A00",
                "src":       20,
                "dst":       0,
                "payload":   payload,
            }
        })
