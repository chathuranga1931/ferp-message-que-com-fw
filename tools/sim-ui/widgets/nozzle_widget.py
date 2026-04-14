"""
NozzleWidget — shows nozzle state (idle / pumping / pumped) and last
transaction values.
"""

import tkinter as tk


_BG = "#1e1e2e"
_IDLE_COLOR    = "#45475a"
_PUMPING_COLOR = "#f9e2af"
_PUMPED_COLOR  = "#a6e3a1"


class NozzleWidget(tk.Frame):
    def __init__(self, parent, label: str = "Nozzle", **kwargs):
        super().__init__(parent, bg=_BG, relief=tk.RIDGE, bd=1, **kwargs)

        self._state = "IDLE"

        # ── Header ────────────────────────────────────────────────────────────
        hdr = tk.Frame(self, bg="#11111b")
        hdr.pack(fill=tk.X)
        self._title = tk.Label(
            hdr, text=label,
            bg="#11111b", fg="#cdd6f4",
            font=("Menlo", 11, "bold"), pady=4)
        self._title.pack()

        # ── State circle ──────────────────────────────────────────────────────
        canvas_frame = tk.Frame(self, bg=_BG)
        canvas_frame.pack(pady=8)

        self._canvas = tk.Canvas(
            canvas_frame, width=60, height=60,
            bg=_BG, highlightthickness=0)
        self._canvas.pack()
        self._circle = self._canvas.create_oval(
            5, 5, 55, 55,
            fill=_IDLE_COLOR, outline="#585b70", width=2)

        self._state_label = tk.Label(
            self, text="IDLE",
            bg=_BG, fg="#6c7086",
            font=("Menlo", 10, "bold"))
        self._state_label.pack()

        # ── Transaction values ────────────────────────────────────────────────
        val_frame = tk.Frame(self, bg=_BG)
        val_frame.pack(fill=tk.X, padx=8, pady=6)

        self._vol_var   = tk.StringVar(value="—")
        self._unit_var  = tk.StringVar(value="—")
        self._total_var = tk.StringVar(value="—")

        rows = [
            ("Volume",     self._vol_var,   "L"),
            ("Unit price", self._unit_var,  ""),
            ("Total",      self._total_var, ""),
        ]
        for i, (lbl, var, unit) in enumerate(rows):
            tk.Label(val_frame, text=f"{lbl}:",
                     bg=_BG, fg="#6c7086",
                     font=("Menlo", 9),
                     anchor=tk.W).grid(row=i, column=0, sticky=tk.W)
            tk.Label(val_frame, textvariable=var,
                     bg=_BG, fg="#cdd6f4",
                     font=("Menlo", 10, "bold"),
                     anchor=tk.E, width=10).grid(row=i, column=1, sticky=tk.E)
            if unit:
                tk.Label(val_frame, text=unit,
                         bg=_BG, fg="#6c7086",
                         font=("Menlo", 9)).grid(row=i, column=2, sticky=tk.W, padx=2)

        val_frame.columnconfigure(1, weight=1)

    # ── Public API ────────────────────────────────────────────────────────────

    def set_state(self, state: str):
        """state: 'IDLE' | 'PUMPING' | 'PUMPED'"""
        self._state = state
        if state == "PUMPING":
            self._canvas.itemconfig(self._circle, fill=_PUMPING_COLOR)
            self._state_label.config(text="PUMPING", fg=_PUMPING_COLOR)
            # clear last values while pumping
            self._vol_var.set("…")
            self._unit_var.set("…")
            self._total_var.set("…")
        elif state == "PUMPED":
            self._canvas.itemconfig(self._circle, fill=_PUMPED_COLOR)
            self._state_label.config(text="PUMPED", fg=_PUMPED_COLOR)
            # Flash back to idle after 3 s
            self.after(3000, lambda: self.set_state("IDLE"))
        else:  # IDLE / STOPPED
            self._canvas.itemconfig(self._circle, fill=_IDLE_COLOR)
            self._state_label.config(text="IDLE", fg="#6c7086")

    def set_pumped(self, vol: float, unit: float, total: float):
        """Call on MsgFuelPumped with final transaction values."""
        self._vol_var.set(f"{vol:.3f} L")
        self._unit_var.set(f"{unit:.2f}")
        self._total_var.set(f"{total:.2f}")
        self.set_state("PUMPED")
