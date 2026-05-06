"""
CloudWidget — shows ModuleCubeSphere (cloud) registration state and event log.

Receives MSG_CUBESPHERE_STATUS data dicts:
    {"event": "REGISTERED"|"REGISTER_FAILED"|"PUMPED_SUCCESS"|"PUMPED_FAILED"
              |"HB_SENT"|"HB_FAILED"|"UNKNOWN",
     "nozzle_idx": <int>,
     "device_uuid": "<uuid or empty>"}
"""

import tkinter as tk
from tkinter import scrolledtext
from datetime import datetime

_BG       = "#1e1e2e"
_FG       = "#cdd6f4"
_DIM      = "#6c7086"
_PANEL    = "#11111b"

# Colour per event
_EVENT_COLORS = {
    "REGISTERED":      "#a6e3a1",   # green
    "PUMPED_SUCCESS":  "#a6e3a1",
    "HB_SENT":         "#89b4fa",   # blue
    "REGISTER_FAILED": "#f38ba8",   # red
    "PUMPED_FAILED":   "#f38ba8",
    "HB_FAILED":       "#fab387",   # orange
    "UNKNOWN":         "#585b70",
}


class CloudWidget(tk.Frame):
    def __init__(self, parent, **kwargs):
        super().__init__(parent, bg=_BG, **kwargs)

        # ── State bar ─────────────────────────────────────────────────────────
        top = tk.Frame(self, bg=_PANEL)
        top.pack(fill=tk.X)

        self._state_label = tk.Label(
            top, text="● NOT REGISTERED",
            bg=_PANEL, fg="#f38ba8",
            font=("Menlo", 11, "bold"), pady=6, padx=10)
        self._state_label.pack(side=tk.LEFT)

        self._hb_label = tk.Label(
            top, text="",
            bg=_PANEL, fg=_DIM,
            font=("Menlo", 9), pady=6, padx=10)
        self._hb_label.pack(side=tk.RIGHT)

        # ── Device UUID row ───────────────────────────────────────────────────
        uuid_row = tk.Frame(self, bg=_BG)
        uuid_row.pack(fill=tk.X, padx=8, pady=(8, 2))

        tk.Label(uuid_row, text="Device UUID:",
                 bg=_BG, fg=_DIM,
                 font=("Menlo", 9), width=14, anchor=tk.W).pack(side=tk.LEFT)

        self._uuid_var = tk.StringVar(value="—")
        self._uuid_entry = tk.Entry(
            uuid_row,
            textvariable=self._uuid_var,
            bg="#313244", fg=_FG,
            readonlybackground="#313244",
            disabledforeground="#a6e3a1",
            font=("Menlo", 9),
            relief=tk.FLAT,
            state="readonly")
        self._uuid_entry.pack(side=tk.LEFT, fill=tk.X, expand=True)

        # ── Stats row ─────────────────────────────────────────────────────────
        stats_row = tk.Frame(self, bg=_BG)
        stats_row.pack(fill=tk.X, padx=8, pady=4)

        self._stat_vars = {}
        for label, key in [("Heartbeats sent:", "hb_sent"),
                            ("Heartbeats failed:", "hb_failed"),
                            ("Pump events sent:", "pumped_ok"),
                            ("Pump events failed:", "pumped_fail")]:
            row = tk.Frame(stats_row, bg=_BG)
            row.pack(fill=tk.X, pady=1)
            tk.Label(row, text=label,
                     bg=_BG, fg=_DIM,
                     font=("Menlo", 9), width=22, anchor=tk.W).pack(side=tk.LEFT)
            var = tk.StringVar(value="0")
            self._stat_vars[key] = var
            tk.Label(row, textvariable=var,
                     bg=_BG, fg=_FG,
                     font=("Menlo", 9)).pack(side=tk.LEFT)

        self._counts = {k: 0 for k in self._stat_vars}

        # ── Event log ─────────────────────────────────────────────────────────
        tk.Label(self, text="Event log:",
                 bg=_BG, fg=_DIM,
                 font=("Menlo", 9)).pack(anchor=tk.W, padx=8, pady=(4, 0))

        self._log = scrolledtext.ScrolledText(
            self, bg=_PANEL, fg=_FG,
            font=("Menlo", 9), height=14,
            state=tk.DISABLED, relief=tk.FLAT)
        self._log.pack(fill=tk.BOTH, expand=True, padx=8, pady=(2, 8))

        # Configure colour tags
        for event, color in _EVENT_COLORS.items():
            self._log.tag_config(event, foreground=color)

        self._hb_count = 0
        self._hb_fail  = 0

    # ── Public API ────────────────────────────────────────────────────────────

    def on_event(self, data: dict):
        event      = data.get("event", "UNKNOWN")
        nozzle_idx = data.get("nozzle_idx", 0)
        uuid       = data.get("device_uuid", "")

        now = datetime.now().strftime("%H:%M:%S")
        color = _EVENT_COLORS.get(event, "#585b70")

        # Update state badge
        badge_text = f"● {event}"
        self._state_label.config(text=badge_text, fg=color)

        # Update UUID if provided
        if uuid:
            self._uuid_var.set(uuid)

        # Update counters
        if event == "HB_SENT":
            self._counts["hb_sent"] += 1
            self._stat_vars["hb_sent"].set(str(self._counts["hb_sent"]))
            self._hb_label.config(
                text=f"last HB: {now}", fg="#89b4fa")
        elif event == "HB_FAILED":
            self._counts["hb_failed"] += 1
            self._stat_vars["hb_failed"].set(str(self._counts["hb_failed"]))
        elif event == "PUMPED_SUCCESS":
            self._counts["pumped_ok"] += 1
            self._stat_vars["pumped_ok"].set(str(self._counts["pumped_ok"]))
        elif event == "PUMPED_FAILED":
            self._counts["pumped_fail"] += 1
            self._stat_vars["pumped_fail"].set(str(self._counts["pumped_fail"]))

        # Build log line
        if event in ("PUMPED_SUCCESS", "PUMPED_FAILED"):
            line = f"[{now}] {event}  nozzle={nozzle_idx}\n"
        elif event == "REGISTERED":
            line = f"[{now}] {event}  uuid={uuid or '?'}\n"
        else:
            line = f"[{now}] {event}\n"

        tag = event if event in _EVENT_COLORS else "UNKNOWN"
        self._log.config(state=tk.NORMAL)
        self._log.insert(tk.END, line, tag)
        self._log.see(tk.END)
        self._log.config(state=tk.DISABLED)
