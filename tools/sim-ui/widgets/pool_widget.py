"""
PoolWidget — bar-graph visualisation of the HSYS memory pool.

Displays one row per pool size-class plus one row for the
message-header pool.  Placeholder rows are shown immediately;
they populate with real values on the first SIM_POOL_STATUS event.

Expected data shape (from simulator JSON):
{
  "classes": [
    {"idx": 0, "block_size": 4,   "total": 8,  "free": 8,  "used": 0},
    {"idx": 1, "block_size": 32,  "total": 16, "free": 15, "used": 1},
    ...
  ],
  "hdr": {"total": 32, "free": 30, "used": 2, "peak": 2}
}
"""

import random
import tkinter as tk
from typing import List


def _util_colour(pct: int) -> str:
    """Green → amber → red based on utilisation %."""
    if pct < 60:
        return "#a6e3a1"
    if pct < 85:
        return "#f9e2af"
    return "#f38ba8"


class _PoolRow(tk.Frame):
    """One labelled canvas-bar row (avoids ttk style conflicts on macOS Tk)."""

    BAR_W  = 220
    BAR_H  = 16
    LABEL_W = 16   # characters

    def __init__(self, parent, label: str, **kwargs):
        super().__init__(parent, bg="#1e1e2e", **kwargs)

        tk.Label(self, text=label, width=self.LABEL_W, anchor=tk.W,
                 bg="#1e1e2e", fg="#cdd6f4",
                 font=("Menlo", 10)).pack(side=tk.LEFT, padx=(0, 4))

        # Canvas bar — drawn manually so colour works on macOS system Tk
        self._canvas = tk.Canvas(self, width=self.BAR_W, height=self.BAR_H,
                                 bg="#313244", highlightthickness=0)
        self._canvas.pack(side=tk.LEFT)
        self._fill = self._canvas.create_rectangle(
            0, 0, 0, self.BAR_H, fill="#a6e3a1", outline="")

        self._detail = tk.Label(self, text="waiting…", width=24, anchor=tk.W,
                                bg="#1e1e2e", fg="#585b70",
                                font=("Menlo", 9))
        self._detail.pack(side=tk.LEFT, padx=(6, 0))

    def update(self, used: int, total: int, peak: int = 0):
        pct    = int(used * 100 / total) if total > 0 else 0
        bar_px = int(self.BAR_W * pct / 100)
        colour = _util_colour(pct)

        self._canvas.coords(self._fill, 0, 0, bar_px, self.BAR_H)
        self._canvas.itemconfig(self._fill, fill=colour)

        detail = f"{used}/{total}  ({pct}%)"
        if peak:
            detail += f"  pk={peak}"
        self._detail.config(text=detail,
                            fg="#a6adc8" if pct < 85 else "#f38ba8")


class PoolWidget(tk.Frame):
    """Container that holds all _PoolRow widgets."""

    # Default pool class layout — matches the firmware's k_pool_table.
    # Rows are shown immediately; labels/totals update on first real message.
    _DEFAULT_CLASSES = [
        (0,   4),
        (1,  32),
        (2,  64),
        (3, 256),
        (4, 512),
    ]

    def __init__(self, parent, **kwargs):
        super().__init__(parent, bg="#1e1e2e", **kwargs)

        tk.Label(self,
                 text="Memory Pool  —  live usage (updates every 5 s)",
                 bg="#1e1e2e", fg="#6c7086",
                 font=("Menlo", 9, "italic")).pack(anchor=tk.W, padx=8, pady=(8, 4))

        tk.Frame(self, bg="#45475a", height=1).pack(fill=tk.X, padx=8, pady=(0, 4))

        # Build placeholder rows immediately
        self._class_rows = []   # type: List[_PoolRow]
        for idx, bsize in self._DEFAULT_CLASSES:
            row = _PoolRow(self, label=f"cls {idx}  ({bsize} B)")
            row.pack(fill=tk.X, padx=8, pady=2)
            self._class_rows.append(row)

        tk.Frame(self, bg="#45475a", height=1).pack(fill=tk.X, padx=8, pady=(6, 4))

        self._hdr_row = _PoolRow(self, label="hdr-pool  (msgs)")
        self._hdr_row.pack(fill=tk.X, padx=8, pady=2)

        self._ts_label = tk.Label(self, text="⏳ waiting for first snapshot…",
                                  bg="#1e1e2e", fg="#585b70",
                                  font=("Menlo", 9, "italic"))
        self._ts_label.pack(anchor=tk.W, padx=8, pady=(8, 4))

    # ── Public API ────────────────────────────────────────────────────────────

    def on_pool_status(self, data: dict):
        """
        Called from App._dispatch() with the 'data' sub-object of a
        SIM_POOL_STATUS message.
        """
        classes = data.get("classes", [])
        hdr     = data.get("hdr", {})
        ts_ms   = data.get("ts_ms", 0)

        # Grow row list if firmware has more classes than our default
        while len(classes) > len(self._class_rows):
            i = len(self._class_rows)
            row = _PoolRow(self, label=f"cls {i}")
            row.pack(fill=tk.X, padx=8, pady=2, before=self._hdr_row)
            self._class_rows.append(row)

        # Update labels and bars
        for i, c in enumerate(classes):
            row = self._class_rows[i]
            # Refresh label with real block_size from firmware
            for widget in row.winfo_children():
                if isinstance(widget, tk.Label) and widget.cget("width") == _PoolRow.LABEL_W:
                    widget.config(text=f"cls {c['idx']}  ({c['block_size']} B)")
                    break
            row.update(used=c.get("used", 0), total=c.get("total", 0))

        if hdr:
            self._hdr_row.update(
                used=hdr.get("used", 0),
                total=hdr.get("total", 0),
                peak=hdr.get("peak", 0))

        if ts_ms:
            secs = ts_ms // 1000
            self._ts_label.config(
                text="✅ last snapshot: t = {} s".format(secs),
                fg="#a6adc8")

    def start_demo(self, interval_ms: int = 2000):
        """Feed random values — used when no simulator is connected."""
        self._demo_interval = interval_ms
        self._demo_tick = 0
        self._run_demo()

    def stop_demo(self):
        if hasattr(self, "_demo_after") and self._demo_after:
            self.after_cancel(self._demo_after)
            self._demo_after = None

    def _run_demo(self):
        totals = [8, 16, 16, 8, 4]
        sizes  = [4, 32, 64, 256, 512]
        classes = []
        for i in range(5):
            total = totals[i]
            used  = random.randint(0, total)
            classes.append({
                "idx": i, "block_size": sizes[i],
                "total": total, "free": total - used, "used": used
            })
        hdr_total = 32
        hdr_used  = random.randint(0, 6)
        self._demo_tick += self._demo_interval
        self.on_pool_status({
            "classes": classes,
            "hdr": {"total": hdr_total, "free": hdr_total - hdr_used,
                    "used": hdr_used, "peak": hdr_used + 1},
            "ts_ms": self._demo_tick,
        })
        self._demo_after = self.after(self._demo_interval, self._run_demo)
