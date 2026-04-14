"""
OtaWidget — shows OTA status per driver and lets you trigger a check.
"""

import tkinter as tk
from tkinter import ttk

_BG = "#1e1e2e"
_FG = "#cdd6f4"

_DRIVER_NAMES = {0: "ESP32-main", 1: "ESP07-coprocessor"}


class OtaWidget(tk.Frame):
    def __init__(self, parent, send_fn, **kwargs):
        super().__init__(parent, bg=_BG, **kwargs)
        self._send = send_fn

        self._driver_frames: dict[int, _DriverCard] = {}

        # ── Driver cards (one per target) ─────────────────────────────────────
        cards = tk.Frame(self, bg=_BG)
        cards.pack(fill=tk.X, padx=6, pady=6)

        for idx, name in _DRIVER_NAMES.items():
            card = _DriverCard(cards, idx, name, send_fn=send_fn)
            card.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=4)
            self._driver_frames[idx] = card

    # ── Public API ────────────────────────────────────────────────────────────

    def on_event(self, data: dict):
        driver = data.get("driver", 0)
        event  = data.get("event", "")
        pct    = data.get("pct", 0)
        ver    = data.get("version", "")

        card = self._driver_frames.get(driver)
        if card:
            card.update(event, pct, ver)


class _DriverCard(tk.LabelFrame):
    def __init__(self, parent, idx: int, name: str, send_fn, **kwargs):
        super().__init__(parent, text=name,
                         bg=_BG, fg=_FG,
                         font=("Menlo", 9, "bold"), **kwargs)
        self._idx    = idx
        self._send   = send_fn

        self._state_label = tk.Label(
            self, text="IDLE",
            bg=_BG, fg="#6c7086",
            font=("Menlo", 11, "bold"))
        self._state_label.pack(pady=4)

        self._progress = ttk.Progressbar(
            self, length=160, maximum=100, value=0)
        self._progress.pack(pady=2)

        self._ver_label = tk.Label(
            self, text="",
            bg=_BG, fg="#6c7086",
            font=("Menlo", 9))
        self._ver_label.pack()

        tk.Button(
            self, text="Trigger OTA check",
            bg="#313244", fg=_FG,
            activebackground="#45475a",
            font=("Menlo", 9), relief=tk.FLAT,
            command=self._trigger).pack(pady=6)

    def update(self, event: str, pct: int, ver: str):
        colour_map = {
            "CHECK_STARTED":      ("#89dceb",  0),
            "CHECK_SUCCESS":      ("#a6e3a1",  0),
            "NO_UPDATE":          ("#6c7086",  0),
            "DOWNLOAD_STARTED":   ("#f9e2af",  5),
            "DOWNLOAD_PROGRESS":  ("#f9e2af", pct),
            "DOWNLOAD_SUCCESS":   ("#a6e3a1", 100),
            "DOWNLOAD_FAILURE":   ("#f38ba8",  0),
        }
        colour, progress = colour_map.get(event, ("#6c7086", 0))
        self._state_label.config(text=event.replace("_", " "), fg=colour)
        self._progress["value"] = progress
        if ver:
            self._ver_label.config(text=f"v{ver}")

    def _trigger(self):
        self._send({
            "id": "SIM_OTA_TRIGGER",
            "data": {"driver": self._idx}
        })
