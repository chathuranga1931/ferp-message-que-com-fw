"""
NozzleWidget — Combined state display + DT board simulator control panel.

For each nozzle the widget provides:
  • State indicator  (IDLE / PUMPING / PUMPED) from MSG_NOZZLE_STATE
  • Nozzle toggle    → sends { "cmd": "SIM_NOZZLE_INPUT", "nozzle": N, "active": bool }
  • Pump button      → starts periodic SIM_DISTAP_FRAME with auto-incrementing
                       volume (mL/s rate) and calculated total price
  • Last transaction readout (populated from MSG_FUEL_PUMPED)
"""

import tkinter as tk
from tkinter import ttk

_BG            = "#1e1e2e"
_BG_DARK       = "#11111b"
_BG_MID        = "#181825"
_FG            = "#cdd6f4"
_FG_DIM        = "#6c7086"
_ACCENT        = "#89b4fa"
_IDLE_COLOR    = "#45475a"
_PUMPING_COLOR = "#f9e2af"
_PUMPED_COLOR  = "#a6e3a1"
_NOZZLE_ON_BG  = "#a6e3a1"   # green  — UP / active
_NOZZLE_ON_FG  = "#1e1e2e"   # dark text on green
_NOZZLE_OFF_BG = "#585b70"   # grey   — DOWN / inactive
_NOZZLE_OFF_FG = "#1e1e2e"   # dark text on grey
_BTN_SEND_BG   = "#89b4fa"   # accent blue for Send Frame / Pump
_BTN_SEND_FG   = "#1e1e2e"   # dark text on blue
_MONO          = ("Menlo", 10)
_MONO_SM       = ("Menlo", 9)
_MONO_LG       = ("Menlo", 11, "bold")

# display_type_t enum values (from display_types.h)
DISPLAY_TYPES: dict[str, int] = {
    "DIS_NONE (0)":           0,
    "CENSTAR 6-digit (1)":    1,
    "CENSTAR 7-digit (2)":    2,
    "CENSTAR 7-digit CS (3)": 3,
    "HONGYANG 8-digit (4)":   4,
    "WAYNE 6-digit (5)":      5,
    "SANKI 6-digit (6)":      6,
    "LONGFENG 8-digit (7)":   7,
}
_DEFAULT_DISPLAY = "SANKI 6-digit (6)"


class NozzleWidget(tk.Frame):
    def __init__(self, parent, label: str = "Nozzle",
                 nozzle_idx: int = 0, send_fn=None, **kwargs):
        super().__init__(parent, bg=_BG, relief=tk.RIDGE, bd=1, **kwargs)
        self._label         = label
        self._nozzle_idx    = nozzle_idx
        self._send_fn       = send_fn or (lambda _: None)
        self._nozzle_active = False   # False = DOWN (inactive), True = UP (active)
        self._auto_job      = None    # after() handle for pump ticking
        self._cont_job      = None    # after() handle for continuous resend
        self._pumping       = False
        self._volume_ml     = 0.0     # accumulated volume in mL (internal units)

        self._build_ui()

    # ── UI construction ───────────────────────────────────────────────────────

    def _build_ui(self):
        # Header
        hdr = tk.Frame(self, bg=_BG_DARK)
        hdr.pack(fill=tk.X)
        tk.Label(hdr, text=self._label, bg=_BG_DARK, fg=_FG,
                 font=_MONO_LG, pady=5).pack()

        # State indicator
        ind = tk.Frame(self, bg=_BG)
        ind.pack(pady=(6, 2))

        self._canvas = tk.Canvas(ind, width=50, height=50,
                                  bg=_BG, highlightthickness=0)
        self._canvas.pack(side=tk.LEFT, padx=8)
        self._circle = self._canvas.create_oval(
            3, 3, 47, 47, fill=_IDLE_COLOR, outline="#585b70", width=2)

        self._state_label = tk.Label(ind, text="IDLE",
                                      bg=_BG, fg=_FG_DIM,
                                      font=("Menlo", 10, "bold"),
                                      width=10, anchor=tk.W)
        self._state_label.pack(side=tk.LEFT)

        # ── Nozzle toggle ─────────────────────────────────────────────────────
        tog = tk.Frame(self, bg=_BG)
        tog.pack(fill=tk.X, padx=8, pady=4)

        tk.Label(tog, text="Nozzle:", bg=_BG, fg=_FG_DIM,
                 font=_MONO_SM).pack(side=tk.LEFT)

        self._nozzle_btn = tk.Button(
            tog, text="▶  Start",
            bg=_NOZZLE_OFF_BG, fg=_NOZZLE_OFF_FG,
            activebackground=_ACCENT, font=_MONO,
            relief=tk.FLAT, padx=8, pady=3,
            command=self._toggle_nozzle)
        self._nozzle_btn.pack(side=tk.LEFT, padx=6)

        ttk.Separator(self, orient=tk.HORIZONTAL).pack(fill=tk.X, padx=4, pady=4)

        # ── Frame builder ─────────────────────────────────────────────────────
        tk.Label(self, text="DT Frame Builder",
                 bg=_BG, fg=_ACCENT,
                 font=("Menlo", 9, "bold")).pack(anchor=tk.W, padx=8)

        flds = tk.Frame(self, bg=_BG)
        flds.pack(fill=tk.X, padx=8, pady=4)

        # Display type row
        tk.Label(flds, text="Display type:", bg=_BG, fg=_FG_DIM,
                 font=_MONO_SM, anchor=tk.W).grid(row=0, column=0, sticky=tk.W, pady=2)
        self._disp_var = tk.StringVar(value=_DEFAULT_DISPLAY)
        disp_menu = ttk.OptionMenu(flds, self._disp_var,
                                    _DEFAULT_DISPLAY, *DISPLAY_TYPES.keys())
        disp_menu.configure(width=22)
        disp_menu.grid(row=0, column=1, columnspan=2, sticky=tk.W, pady=2)

        # Unit price (only manual field — volume/total are calculated)
        tk.Label(flds, text="unit_price ×100:", bg=_BG, fg=_FG_DIM,
                 font=_MONO_SM, anchor=tk.W).grid(row=1, column=0, sticky=tk.W, pady=1)
        self._unit_price_var = tk.StringVar(value="300")
        tk.Entry(flds, textvariable=self._unit_price_var,
                 bg=_BG_MID, fg=_FG, insertbackground=_FG,
                 font=_MONO, width=10, relief=tk.FLAT).grid(
                     row=1, column=1, sticky=tk.W, padx=4, pady=1)
        tk.Label(flds, text="e.g. 300 = $3.00/L", bg=_BG, fg=_FG_DIM,
                 font=("Menlo", 8)).grid(row=1, column=2, sticky=tk.W)

        # Vol rate (mL per second)
        tk.Label(flds, text="Vol rate (mL/s):", bg=_BG, fg=_FG_DIM,
                 font=_MONO_SM, anchor=tk.W).grid(row=2, column=0, sticky=tk.W, pady=1)
        self._vol_rate_var = tk.StringVar(value="500")
        tk.Entry(flds, textvariable=self._vol_rate_var,
                 bg=_BG_MID, fg=_FG, insertbackground=_FG,
                 font=_MONO, width=10, relief=tk.FLAT).grid(
                     row=2, column=1, sticky=tk.W, padx=4, pady=1)
        tk.Label(flds, text="volume per second", bg=_BG, fg=_FG_DIM,
                 font=("Menlo", 8)).grid(row=2, column=2, sticky=tk.W)

        flds.columnconfigure(1, weight=1)

        # ── Rate row ──────────────────────────────────────────────────────────
        rate_row = tk.Frame(self, bg=_BG)
        rate_row.pack(fill=tk.X, padx=8, pady=(4, 2))

        tk.Label(rate_row, text="Interval (ms):", bg=_BG, fg=_FG_DIM,
                 font=_MONO_SM).pack(side=tk.LEFT)
        self._rate_var = tk.StringVar(value="100")
        tk.Entry(rate_row, textvariable=self._rate_var,
                 bg=_BG_MID, fg=_FG, insertbackground=_FG,
                 font=_MONO, width=6, relief=tk.FLAT).pack(side=tk.LEFT, padx=4)

        # Live volume readout
        live_row = tk.Frame(self, bg=_BG_MID)
        live_row.pack(fill=tk.X, padx=8, pady=(2, 4))
        tk.Label(live_row, text="Volume now:", bg=_BG_MID, fg=_FG_DIM,
                 font=_MONO_SM).pack(side=tk.LEFT, padx=6, pady=3)
        self._live_vol_var = tk.StringVar(value="0.000 L")
        tk.Label(live_row, textvariable=self._live_vol_var,
                 bg=_BG_MID, fg=_PUMPING_COLOR,
                 font=("Menlo", 10, "bold")).pack(side=tk.LEFT, padx=4)

        # ── Pump button (toggle: start / stop continuous sending) ─────────────
        self._pump_btn = tk.Button(
            self, text="▶  Pump",
            bg=_BTN_SEND_BG, fg=_BTN_SEND_FG,
            activebackground="#74c7ec",
            font=("Menlo", 11, "bold"), relief=tk.FLAT,
            padx=8, pady=6,
            command=self._toggle_pump)
        self._pump_btn.pack(fill=tk.X, padx=8, pady=(2, 4))

        # ── Continuous data checkbox ──────────────────────────────────────────
        cont_row = tk.Frame(self, bg=_BG)
        cont_row.pack(fill=tk.X, padx=8, pady=(0, 4))
        self._continuous_var = tk.BooleanVar(value=False)
        tk.Checkbutton(
            cont_row, text="Continuous data  (keep sending last frame)",
            variable=self._continuous_var,
            bg=_BG, fg=_FG_DIM, selectcolor=_BG_MID,
            activebackground=_BG, activeforeground=_FG,
            font=_MONO_SM,
            command=self._on_continuous_changed,
        ).pack(side=tk.LEFT)

        ttk.Separator(self, orient=tk.HORIZONTAL).pack(fill=tk.X, padx=4, pady=2)

        # ── Last transaction ──────────────────────────────────────────────────
        tk.Label(self, text="Last Transaction",
                 bg=_BG, fg=_ACCENT,
                 font=("Menlo", 9, "bold")).pack(anchor=tk.W, padx=8, pady=(4, 0))

        tx = tk.Frame(self, bg=_BG)
        tx.pack(fill=tk.X, padx=8, pady=4)

        self._tx_vol_var   = tk.StringVar(value="—")
        self._tx_unit_var  = tk.StringVar(value="—")
        self._tx_total_var = tk.StringVar(value="—")

        for i, (lbl, var) in enumerate([
            ("Volume",     self._tx_vol_var),
            ("Unit price", self._tx_unit_var),
            ("Total",      self._tx_total_var),
        ]):
            tk.Label(tx, text=f"{lbl}:", bg=_BG, fg=_FG_DIM,
                     font=_MONO_SM, anchor=tk.W).grid(row=i, column=0, sticky=tk.W)
            tk.Label(tx, textvariable=var, bg=_BG, fg=_PUMPED_COLOR,
                     font=_MONO, anchor=tk.E, width=14).grid(row=i, column=1, sticky=tk.E)

        tx.columnconfigure(1, weight=1)

    # ── Public API (called by sim_ui._dispatch) ───────────────────────────────

    def set_state(self, state: str):
        """state: 'IDLE' | 'PUMPING' | 'PUMPED'"""
        if state == "PUMPING":
            self._canvas.itemconfig(self._circle, fill=_PUMPING_COLOR)
            self._state_label.config(text="PUMPING", fg=_PUMPING_COLOR)
        elif state == "PUMPED":
            self._canvas.itemconfig(self._circle, fill=_PUMPED_COLOR)
            self._state_label.config(text="PUMPED", fg=_PUMPED_COLOR)
            self.after(3000, lambda: self.set_state("IDLE"))
        else:
            self._canvas.itemconfig(self._circle, fill=_IDLE_COLOR)
            self._state_label.config(text="IDLE", fg=_FG_DIM)

    def set_pumped(self, vol: float, unit: float, total: float):
        """Call on MsgFuelPumped with final transaction values."""
        self._tx_vol_var.set(f"{vol:.3f} L")
        self._tx_unit_var.set(f"${unit / 100:.2f}/L")
        self._tx_total_var.set(f"${total / 100:.2f}")
        self.set_state("PUMPED")

    # ── Internal ──────────────────────────────────────────────────────────────

    def _toggle_nozzle(self):
        self._nozzle_active = not self._nozzle_active
        if self._nozzle_active:
            # Nozzle START pressed → going UP
            # Save last transaction values before clearing (if total is non-zero)
            self._save_last_transaction()
            # Reset volume accumulator for the new transaction
            self._volume_ml = 0.0
            self._live_vol_var.set("0.000 L")
            # Update button and status circle
            self._nozzle_btn.config(text="■  Stop", bg=_NOZZLE_ON_BG, fg=_NOZZLE_ON_FG)
            self._canvas.itemconfig(self._circle, fill=_NOZZLE_ON_BG)
            self._state_label.config(text="Nozzle UP", fg=_NOZZLE_ON_BG)
        else:
            # Nozzle STOP pressed → going DOWN
            self._nozzle_btn.config(text="▶  Start", bg=_NOZZLE_OFF_BG, fg=_NOZZLE_OFF_FG)
            self._canvas.itemconfig(self._circle, fill=_IDLE_COLOR)
            self._state_label.config(text="Nozzle Down", fg=_FG_DIM)
            self._stop_continuous()
        self._send_fn({
            "cmd":    "SIM_NOZZLE_INPUT",
            "nozzle": self._nozzle_idx,
            "active": not self._nozzle_active,  # inverted: UP=false(low→release), DOWN=true(high→press)
        })

    def _save_last_transaction(self):
        """Snapshot current volume/unit/total → Last Transaction (skipped if total=0)."""
        try:
            unit_price_x100 = int(self._unit_price_var.get())
            volume_l_x1000  = int(round(self._volume_ml))
            total_x100      = (unit_price_x100 * volume_l_x1000) // 1000
            if total_x100 > 0:
                self._tx_vol_var.set(f"{volume_l_x1000 / 1000.0:.3f} L")
                self._tx_unit_var.set(f"${unit_price_x100 / 100:.2f}/L")
                self._tx_total_var.set(f"${total_x100 / 100:.2f}")
        except (ValueError, ZeroDivisionError):
            pass

    def _send_frame(self, flags: int = 1) -> bool:
        """Build and send one SIM_DISTAP_FRAME using current volume accumulator."""
        try:
            unit_price_x100 = int(self._unit_price_var.get())
            volume_l_x1000  = int(round(self._volume_ml))
            # total_x100 = unit_price (per L) × volume_l
            total_x100 = (unit_price_x100 * volume_l_x1000) // 1000
            payload = {
                "cmd":          "SIM_DISTAP_FRAME",
                "nozzle":       self._nozzle_idx,
                "display_type": DISPLAY_TYPES[self._disp_var.get()],
                "flags":        flags,
                "error":        0,
                "unit_price":   unit_price_x100,
                "total_price":  total_x100,
                "volume_l":     volume_l_x1000,
            }
            self._send_fn(payload)
            return True
        except (ValueError, KeyError):
            self._stop_pump()
            self._pump_btn.config(bg="#f38ba8", fg="#1e1e2e")
            self.after(800, lambda: self._pump_btn.config(
                bg=_BTN_SEND_BG, fg=_BTN_SEND_FG))
            return False

    def _toggle_pump(self):
        if self._pumping:
            self._stop_pump()
        else:
            self._start_pump()

    def _start_pump(self):
        try:
            interval = max(10, int(self._rate_var.get()))
        except ValueError:
            interval = 100
        self._rate_var.set(str(interval))
        # Stop continuous resend — pump loop takes over
        self._stop_continuous()
        self._pumping = True
        self._pump_btn.config(text="■  Stop", bg="#f38ba8", fg="#1e1e2e")
        self._pump_tick(interval)

    def _stop_pump(self):
        self._pumping = False
        if self._auto_job is not None:
            self.after_cancel(self._auto_job)
            self._auto_job = None
        self._pump_btn.config(text="▶  Pump", bg=_BTN_SEND_BG, fg=_BTN_SEND_FG)
        # Send final frame with flags=0 (stop signal to state machine)
        self._send_frame(flags=0)
        # If continuous mode is on, keep resending the last frame value
        if self._continuous_var.get():
            self._start_continuous()

    def _on_continuous_changed(self):
        """Called when the Continuous checkbox is toggled."""
        if self._continuous_var.get():
            # Start only if not currently pumping
            if not self._pumping:
                self._start_continuous()
        else:
            self._stop_continuous()

    def _start_continuous(self):
        """Start a repeating job that resends the last frame at the set interval."""
        self._stop_continuous()   # cancel any existing job first
        try:
            interval = max(10, int(self._rate_var.get()))
        except ValueError:
            interval = 100
        self._cont_tick(interval)

    def _stop_continuous(self):
        if self._cont_job is not None:
            self.after_cancel(self._cont_job)
            self._cont_job = None

    def _cont_tick(self, interval: int):
        """Resend the last accumulated frame value (flags=1) continuously."""
        if not self._continuous_var.get():
            self._cont_job = None
            return
        self._send_frame(flags=1)
        self._cont_job = self.after(interval, lambda: self._cont_tick(interval))

    def _pump_tick(self, interval: int):
        if not self._pumping:
            return
        try:
            rate_ml_s = float(self._vol_rate_var.get())
        except ValueError:
            rate_ml_s = 500.0
        # Accumulate volume based on elapsed interval
        self._volume_ml += rate_ml_s * (interval / 1000.0)
        self._live_vol_var.set(f"{self._volume_ml / 1000.0:.3f} L")
        if self._send_frame(flags=1):
            self._auto_job = self.after(interval, lambda: self._pump_tick(interval))
        else:
            self._pumping = False
            self._pump_btn.config(text="▶  Pump", bg=_BTN_SEND_BG, fg=_BTN_SEND_FG)

    def _toggle_auto(self):
        self._toggle_pump()

    def _start_auto(self):
        self._start_pump()

    def _stop_auto(self):
        self._stop_pump()
