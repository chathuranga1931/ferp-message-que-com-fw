"""
LedWidget — a labelled coloured circle that can be on/off/blinking.
"""

import tkinter as tk
from typing import Optional


class LedWidget(tk.Frame):
    _RADIUS = 9

    def __init__(self, parent, label: str,
                 color_on: str  = "#a6e3a1",
                 color_off: str = "#313244",
                 **kwargs):
        super().__init__(parent, bg="#1e1e2e", **kwargs)

        self._color_on  = color_on
        self._color_off = color_off
        self._is_on     = False
        self._flash_job = None

        r = self._RADIUS
        size = r * 2 + 4

        self._canvas = tk.Canvas(
            self, width=size, height=size,
            bg="#1e1e2e", highlightthickness=0)
        self._canvas.pack(side=tk.LEFT)

        self._circle = self._canvas.create_oval(
            2, 2, size - 2, size - 2,
            fill=color_off, outline="#45475a", width=1)

        tk.Label(
            self, text=label,
            bg="#1e1e2e", fg="#cdd6f4",
            font=("Menlo", 10)
        ).pack(side=tk.LEFT, padx=4)

    # ── Public API ────────────────────────────────────────────────────────────

    def set_on(self, state: bool):
        """Turn the LED on or off."""
        if self._flash_job is not None:
            self.after_cancel(self._flash_job)
            self._flash_job = None
        self._is_on = state
        self._redraw()

    def flash(self, duration_ms: int = 300,
              color: Optional[str] = None,
              times: int = 1):
        """Flash the LED on for `duration_ms` then restore previous state."""
        prev = self._is_on
        prev_color = self._color_on
        if color:
            self._color_on = color
        self._canvas.itemconfig(self._circle, fill=self._color_on)

        def _restore():
            self._color_on = prev_color
            self._is_on = prev
            self._redraw()
            self._flash_job = None

        self._flash_job = self.after(duration_ms, _restore)

    def blink(self, period_ms: int = 500):
        """Toggle the LED every `period_ms` milliseconds."""
        self._is_on = not self._is_on
        self._redraw()
        self._flash_job = self.after(period_ms, lambda: self.blink(period_ms))

    def stop_blink(self):
        if self._flash_job is not None:
            self.after_cancel(self._flash_job)
            self._flash_job = None

    # ── Private ───────────────────────────────────────────────────────────────

    def _redraw(self):
        color = self._color_on if self._is_on else self._color_off
        self._canvas.itemconfig(self._circle, fill=color)
