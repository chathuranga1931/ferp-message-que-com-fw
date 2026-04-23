"""
InternetWidget — shows internet reachability state and lets you inject events.

Displays:
  - ONLINE / OFFLINE badge
  - Timestamp of last state change
  - Inject buttons: simulate ONLINE / OFFLINE
"""

import tkinter as tk
from tkinter import ttk
import time


class InternetWidget(tk.Frame):

    _COLORS = {
        "ONLINE":  "#a6e3a1",   # green
        "OFFLINE": "#f38ba8",   # red
    }

    def __init__(self, parent, send_fn=None, **kwargs):
        super().__init__(parent, bg="#1e1e2e", **kwargs)
        self._send       = send_fn
        self._state      = "OFFLINE"
        self._change_ts  = None
        self._build_ui()

    # ── Build ─────────────────────────────────────────────────────────────────

    def _build_ui(self):
        # ── Status banner ─────────────────────────────────────────────────────
        banner = tk.Frame(self, bg="#1e1e2e")
        banner.pack(fill=tk.X, padx=8, pady=(8, 4))

        tk.Label(banner, text="Internet Status",
                 bg="#1e1e2e", fg="#cdd6f4",
                 font=("Menlo", 11, "bold")).pack(side=tk.LEFT)

        self._badge = tk.Label(
            banner, text="OFFLINE",
            bg="#f38ba8", fg="#1e1e2e",
            font=("Menlo", 10, "bold"), padx=8, pady=3)
        self._badge.pack(side=tk.LEFT, padx=8)

        # ── Info grid ─────────────────────────────────────────────────────────
        info = tk.LabelFrame(self, text="Reachability Info",
                              bg="#1e1e2e", fg="#cdd6f4",
                              font=("Menlo", 10, "bold"))
        info.pack(fill=tk.X, padx=8, pady=4)

        def row(label, default="—"):
            r = tk.Frame(info, bg="#1e1e2e")
            r.pack(fill=tk.X, padx=6, pady=3)
            tk.Label(r, text=f"{label}:", bg="#1e1e2e", fg="#6c7086",
                     font=("Menlo", 9), width=14, anchor=tk.W).pack(side=tk.LEFT)
            v = tk.Label(r, text=default, bg="#1e1e2e", fg="#cdd6f4",
                         font=("Menlo", 9), anchor=tk.W)
            v.pack(side=tk.LEFT)
            return v

        self._lbl_state      = row("State",          "OFFLINE")
        self._lbl_last_change = row("Last change",   "—")
        self._lbl_uptime     = row("Online since",   "—")

        # ── History list ──────────────────────────────────────────────────────
        hist = tk.LabelFrame(self, text="Event History",
                              bg="#1e1e2e", fg="#cdd6f4",
                              font=("Menlo", 10, "bold"))
        hist.pack(fill=tk.BOTH, expand=True, padx=8, pady=4)

        self._history = tk.Listbox(
            hist,
            bg="#181825", fg="#cdd6f4",
            selectbackground="#313244",
            font=("Menlo", 9),
            relief=tk.FLAT, height=8)
        self._history.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        # ── Inject buttons ────────────────────────────────────────────────────
        btn_frame = tk.LabelFrame(self, text="Inject Event",
                                   bg="#1e1e2e", fg="#cdd6f4",
                                   font=("Menlo", 10, "bold"))
        btn_frame.pack(fill=tk.X, padx=8, pady=(4, 8))

        row_btns = tk.Frame(btn_frame, bg="#1e1e2e")
        row_btns.pack(fill=tk.X, padx=6, pady=4)

        for label, connected in (("🌐 Inject ONLINE", True), ("❌ Inject OFFLINE", False)):
            tk.Button(
                row_btns, text=label,
                bg="#313244", fg="#cdd6f4",
                activebackground="#89b4fa",
                font=("Menlo", 10), relief=tk.FLAT,
                padx=6, pady=4,
                command=lambda c=connected: self._inject(c)
            ).pack(side=tk.LEFT, fill=tk.X, expand=True, padx=4)

    # ── Public API ────────────────────────────────────────────────────────────

    def on_internet_status(self, data: dict):
        """Called from sim_ui._dispatch when MSG_INTERNET_STATUS arrives."""
        connected = data.get("connected", False)
        new_state = "ONLINE" if connected else "OFFLINE"

        ts_str = time.strftime("%H:%M:%S")

        if new_state != self._state:
            self._state     = new_state
            self._change_ts = time.time() if connected else None

            color = self._COLORS[new_state]
            self._badge.config(text=new_state, bg=color)
            self._lbl_state.config(text=new_state, fg=color)
            self._lbl_last_change.config(text=ts_str)

            if connected:
                self._lbl_uptime.config(text=ts_str)
            else:
                self._lbl_uptime.config(text="—")

            # Add to history (newest at top)
            icon = "🌐" if connected else "❌"
            self._history.insert(0, f"{ts_str}  {icon}  {new_state}")
            # Keep last 50 entries
            if self._history.size() > 50:
                self._history.delete(50, tk.END)

    # ── Inject ────────────────────────────────────────────────────────────────

    def _inject(self, connected: bool):
        if not self._send:
            return
        self._send({
            "id":   "SIM_MSG_INJECT",
            "data": {
                "msg_id":  "0x0A01",   # MSG_ID_INTERNET_STATUS
                "src":     20,          # ModuleSimBridge
                "dst":     0,
                "payload": {"connected": connected},
            }
        })
