"""
LogWidget — scrolling colour-coded log panel.
"""

import json
import tkinter as tk
from tkinter import scrolledtext
from datetime import datetime


_BG      = "#11111b"
_FG      = "#cdd6f4"
_TAG_MAP = {
    # id prefix → (foreground colour)
    "MSG_TICK":        "#585b70",
    "MSG_WIFI":        "#89dceb",
    "MSG_INTERNET":    "#89b4fa",
    "MSG_NOZZLE":      "#f9e2af",
    "MSG_FUEL":        "#a6e3a1",
    "MSG_CLOUD":       "#cba6f7",
    "MSG_MQTT":        "#fab387",
    "MSG_OTA":         "#f38ba8",
    "SIM_":            "#6c7086",
    "_LOG":            "#cdd6f4",
    "_SIM_ERROR":      "#f38ba8",
    "_SIM_DISCONNECT": "#f38ba8",
}


class LogWidget(tk.Frame):
    MAX_LINES = 2000

    def __init__(self, parent, **kwargs):
        super().__init__(parent, bg=_BG, **kwargs)

        toolbar = tk.Frame(self, bg=_BG)
        toolbar.pack(fill=tk.X, padx=4, pady=2)

        self._filter_var = tk.StringVar()
        tk.Label(toolbar, text="Filter:", bg=_BG, fg="#6c7086",
                 font=("Menlo", 9)).pack(side=tk.LEFT)
        tk.Entry(toolbar, textvariable=self._filter_var,
                 bg="#313244", fg=_FG, insertbackground=_FG,
                 font=("Menlo", 9), width=20,
                 relief=tk.FLAT).pack(side=tk.LEFT, padx=4)

        tk.Button(toolbar, text="Clear",
                  bg="#313244", fg=_FG,
                  activebackground="#45475a",
                  font=("Menlo", 9), relief=tk.FLAT,
                  command=self._clear).pack(side=tk.RIGHT, padx=4)

        self._text = scrolledtext.ScrolledText(
            self, bg=_BG, fg=_FG,
            font=("Menlo", 9),
            state=tk.DISABLED,
            wrap=tk.WORD,
            relief=tk.FLAT)
        self._text.pack(fill=tk.BOTH, expand=True)

        # Configure colour tags
        for prefix, colour in _TAG_MAP.items():
            self._text.tag_config(prefix, foreground=colour)
        self._text.tag_config("dim",  foreground="#45475a")
        self._text.tag_config("warn", foreground="#f9e2af")
        self._text.tag_config("err",  foreground="#f38ba8")

        self._line_count = 0

    # ── Public API ────────────────────────────────────────────────────────────

    def append(self, obj: dict):
        """Append a parsed JSON event object."""
        msg_id = obj.get("id", "?")
        ts     = obj.get("ts", 0)
        data   = obj.get("data", {})

        filt = self._filter_var.get().lower()
        if filt and filt not in msg_id.lower() and filt not in json.dumps(data).lower():
            return

        tag = self._pick_tag(msg_id)
        now = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        line = f"[{now}] {msg_id:30s} {json.dumps(data)}\n"
        self._insert(line, tag)

    def append_text(self, text: str, color: str = _FG):
        now  = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        line = f"[{now}] {text}\n"
        tag  = f"_custom_{color}"
        self._text.tag_config(tag, foreground=color)
        self._insert(line, tag)

    # ── Private ───────────────────────────────────────────────────────────────

    def _pick_tag(self, msg_id: str) -> str:
        for prefix, _ in _TAG_MAP.items():
            if msg_id.startswith(prefix):
                return prefix
        return "dim"

    def _insert(self, line: str, tag: str):
        self._text.config(state=tk.NORMAL)
        self._text.insert(tk.END, line, tag)
        self._line_count += 1
        # Prune old lines
        if self._line_count > self.MAX_LINES:
            self._text.delete("1.0", "500.0")
            self._line_count -= 500
        self._text.see(tk.END)
        self._text.config(state=tk.DISABLED)

    def _clear(self):
        self._text.config(state=tk.NORMAL)
        self._text.delete("1.0", tk.END)
        self._text.config(state=tk.DISABLED)
        self._line_count = 0
