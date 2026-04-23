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

    _COMPACT_BAR_H = 4
    _NORMAL_BAR_H  = 14
    _COMPACT_BAR_W = 210
    _NORMAL_BAR_W  = 220

    def __init__(self, parent, label: str, compact: bool = False, **kwargs):
        super().__init__(parent, bg="#1e1e2e", **kwargs)
        self._compact = compact

        if compact:
            # ── Stacked layout: label+detail on top, thin bar below ───────────
            hdr = tk.Frame(self, bg="#1e1e2e")
            hdr.pack(fill=tk.X)

            tk.Label(hdr, text=label, anchor=tk.W,
                     bg="#1e1e2e", fg="#cdd6f4",
                     font=("Menlo", 9)).pack(side=tk.LEFT)

            self._detail = tk.Label(hdr, text="", anchor=tk.E,
                                    bg="#1e1e2e", fg="#585b70",
                                    font=("Menlo", 8))
            self._detail.pack(side=tk.RIGHT, padx=(0, 2))

            bar_h = self._COMPACT_BAR_H
            bar_w = self._COMPACT_BAR_W
        else:
            # ── Horizontal layout: label | bar | detail ───────────────────────
            tk.Label(self, text=label, width=16, anchor=tk.W,
                     bg="#1e1e2e", fg="#cdd6f4",
                     font=("Menlo", 10)).pack(side=tk.LEFT, padx=(0, 4))

            self._detail = tk.Label(self, text="waiting…", width=24, anchor=tk.W,
                                    bg="#1e1e2e", fg="#585b70",
                                    font=("Menlo", 9))

            bar_h = self._NORMAL_BAR_H
            bar_w = self._NORMAL_BAR_W

        # Canvas bar — drawn manually so colour works on macOS system Tk
        self._canvas = tk.Canvas(self, width=bar_w, height=bar_h,
                                 bg="#313244", highlightthickness=0)
        self._canvas.pack(side=tk.LEFT if not compact else tk.TOP,
                          fill=tk.X if compact else tk.NONE,
                          pady=(1, 2) if compact else 0)
        self._fill = self._canvas.create_rectangle(
            0, 0, 0, bar_h, fill="#a6e3a1", outline="")
        # Peak watermark tick (vertical line, shown on hdr-pool)
        self._peak_tick = self._canvas.create_line(
            0, 0, 0, bar_h, fill="#f9e2af", width=1, state=tk.HIDDEN)
        self._bar_w = bar_w
        self._bar_h = bar_h

        if not compact:
            self._detail.pack(side=tk.LEFT, padx=(6, 0))

    def update(self, used: int, total: int, peak: int = 0):
        pct    = int(used * 100 / total) if total > 0 else 0
        bar_px = int(self._bar_w * pct / 100)
        colour = _util_colour(pct)

        self._canvas.coords(self._fill, 0, 0, bar_px, self._bar_h)
        self._canvas.itemconfig(self._fill, fill=colour)

        # Peak watermark tick
        if peak > 0 and total > 0:
            pk_px = int(self._bar_w * peak / total)
            self._canvas.coords(self._peak_tick, pk_px, 0, pk_px, self._bar_h)
            self._canvas.itemconfig(self._peak_tick, state=tk.NORMAL)
        else:
            self._canvas.itemconfig(self._peak_tick, state=tk.HIDDEN)

        detail = f"{used}/{total} ({pct}%)"
        if peak:
            detail += f" pk:{peak}"
        self._detail.config(text=detail,
                            fg="#a6adc8" if pct < 85 else "#f38ba8")


class PoolWidget(tk.Frame):
    """Container that holds all _PoolRow widgets.
    Rows are created dynamically from the first SIM_POOL_STATUS message,
    so the UI automatically reflects whatever pool table the C++ side defines.
    """

    def __init__(self, parent, compact: bool = False, **kwargs):
        super().__init__(parent, bg="#1e1e2e", **kwargs)
        self._compact = compact

        tk.Label(self,
                 text="Memory Pool \u2014 live usage",
                 bg="#1e1e2e", fg="#6c7086",
                 font=("Menlo", 9, "italic")).pack(anchor=tk.W, padx=8, pady=(8, 4))

        self._divider_top = tk.Frame(self, bg="#45475a", height=1)
        self._divider_top.pack(fill=tk.X, padx=8, pady=(0, 4))

        # Rows are built on first on_pool_status() — nothing hardcoded here
        self._class_rows = []   # type: List[_PoolRow]

        self._divider_bot = tk.Frame(self, bg="#45475a", height=1)
        # packed after first data arrives

        self._hdr_row = _PoolRow(self, label="hdr-pool", compact=compact)
        # packed after first data arrives

        # Status + Test button row
        foot = tk.Frame(self, bg="#1e1e2e")
        foot.pack(fill=tk.X, padx=8, pady=(8, 4))

        self._ts_label = tk.Label(foot, text="\u23f3 waiting for simulator\u2026",
                                  bg="#1e1e2e", fg="#585b70",
                                  font=("Menlo", 9, "italic"))
        self._ts_label.pack(side=tk.LEFT)

        tk.Button(foot, text="Test",
                  bg="#313244", fg="#6c7086",
                  activebackground="#45475a",
                  font=("Menlo", 8), relief=tk.FLAT,
                  padx=4, pady=1,
                  command=self._inject_test).pack(side=tk.RIGHT)

        self._rows_built = False

    # ── Public API ────────────────────────────────────────────────────────────

    def on_pool_status(self, data: dict):
        """
        Called from App._dispatch() with the 'data' sub-object of a
        SIM_POOL_STATUS message.

        Compact format: {"c": [[used, total, peak], ...], "h": [used, total, peak]}
        Rows are created on first call to match exactly what the firmware reports.
        """
        classes = data.get("c", [])   # list of [used, total, peak]
        hdr     = data.get("h", [])   # [used, total, peak]

        # ── First message: build rows from live firmware data ─────────────────
        if not self._rows_built and classes:
            for i in range(len(classes)):
                row = _PoolRow(self, label=f"cls {i}", compact=self._compact)
                row.pack(fill=tk.X, padx=8, pady=2)
                self._class_rows.append(row)
            self._divider_bot.pack(fill=tk.X, padx=8, pady=(6, 4))
            self._hdr_row.pack(fill=tk.X, padx=8, pady=2)
            self._rows_built = True

        # ── Grow if firmware added classes at runtime ─────────────────────────
        while len(classes) > len(self._class_rows):
            i = len(self._class_rows)
            row = _PoolRow(self, label=f"cls {i}", compact=self._compact)
            row.pack(fill=tk.X, padx=8, pady=2, before=self._divider_bot)
            self._class_rows.append(row)

        # ── Update bars ───────────────────────────────────────────────────────
        for i, c in enumerate(classes):
            used  = c[0]
            total = c[1]
            peak  = c[2] if len(c) > 2 else 0
            self._class_rows[i].update(used=used, total=total, peak=peak)

        if len(hdr) >= 2:
            self._hdr_row.update(
                used=hdr[0],
                total=hdr[1],
                peak=hdr[2] if len(hdr) > 2 else 0)

        self._ts_label.config(text="✅ live", fg="#a6adc8")

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
        totals = [8, 32, 32, 24, 8]
        c = [[random.randint(0, t), t, random.randint(0, t)] for t in totals]
        hdr_total = 32
        hdr_used  = random.randint(0, 6)
        self._demo_tick += self._demo_interval
        self.on_pool_status({
            "c": c,
            "h": [hdr_used, hdr_total, max(hdr_used, 6)],
        })
        self._demo_after = self.after(self._demo_interval, self._run_demo)

    def _inject_test(self):
        """Inject hardcoded non-zero data to visually verify bars render."""
        self.on_pool_status({
            "c": [[2, 8, 4], [10, 32, 18], [12, 32, 20], [6, 24, 10], [3, 8, 5]],
            "h": [4, 32, 6],
        })
