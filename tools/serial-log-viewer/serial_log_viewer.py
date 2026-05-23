#!/usr/bin/env python3
"""
FERP Serial Log Viewer
======================
A serial terminal customised for the HSYS firmware log format.

Log line format (after ANSI stripping):
    288822  756 INF FUEL       458 : Received frame: ...
    288822  756 ERR [ModuleHttp] request failed

Levels  : ERR  DEB  WAR  INF  PLAIN (unrecognised / non-log lines)
Tags    : 8-char raw tag (e.g. FUEL, WIFICNX) or module name ([ModuleHttp])

Usage:
    sh run.mac.sh
    — or —
    python3 serial_log_viewer.py
"""

import os
os.environ.setdefault("TK_SILENCE_DEPRECATION", "1")

import json
import queue
import re
import sys
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial not found.  Run:  pip install pyserial")
    sys.exit(1)


# ─── Paths ────────────────────────────────────────────────────────────────────
SCRIPT_DIR  = Path(__file__).resolve().parent
CONFIG_PATH = SCRIPT_DIR / "config.json"

# ─── Tuning ───────────────────────────────────────────────────────────────────
MAX_LIVE_LINES   = 5_000   # trim live log after this many lines
TRIM_CHUNK       = 500     # number of lines to remove when trimming
POLL_MS          = 40      # UI poll interval in milliseconds
BATCH            = 150     # max lines processed per poll tick

# ─── Colours (Catppuccin Mocha) ───────────────────────────────────────────────
BG     = "#1e1e2e"
BG2    = "#313244"
BG3    = "#45475a"
FG     = "#cdd6f4"
DIM    = "#6c7086"
RED    = "#f38ba8"
GREEN  = "#a6e3a1"
YELLOW = "#f9e2af"
BLUE   = "#89b4fa"
MAUVE  = "#cba6f7"
TEAL   = "#94e2d5"

LEVEL_COLOR: dict[str, str] = {
    "ERR":   RED,
    "DEB":   GREEN,
    "WAR":   YELLOW,
    "INF":   BLUE,
    "PLAIN": DIM,
}

# ─── ANSI / log parsing ───────────────────────────────────────────────────────
_ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")

# After ANSI-stripping the line looks like:
#   "288822  756 INF FUEL       458 : Received frame: ..."
#   "  1234  567 INF [ModuleHttp] request failed"
_LOG_RE = re.compile(
    r"^\s*\d+\s+\d+\s+"              # ts_ms  log_idx  (header)
    r"(?P<level>ERR|DEB|WAR|INF)\s+" # level keyword
    r"(?:"
        r"(?P<mod_tag>\[[^\]]+\])"   # module tag  [Name]
        r"|(?P<raw_tag>\S+)"         # raw tag     FUEL  WIFICNX  …
    r")"
    r"(?P<rest>.*)",                  # everything after the tag
    re.DOTALL,
)


def parse_line(raw: str) -> dict:
    """Return a dict: level, tag, plain_text."""
    plain = _ANSI_RE.sub("", raw).rstrip()
    m = _LOG_RE.match(plain)
    if not m:
        return dict(level="PLAIN", tag="", plain_text=plain)
    level = m.group("level")
    tag   = (m.group("mod_tag") or m.group("raw_tag") or "").strip()
    return dict(level=level, tag=tag, plain_text=plain)


# ─── Config ───────────────────────────────────────────────────────────────────
_DEFAULT_CFG: dict = {
    "port":          "",
    "baudrate":      115200,
    "level_filters": {"ERR": True, "DEB": True, "WAR": True,
                      "INF": True, "PLAIN": True},
    "tag_filters":   {},
}


def _load_cfg() -> dict:
    if CONFIG_PATH.exists():
        try:
            with CONFIG_PATH.open() as fh:
                stored = json.load(fh)
            cfg = dict(_DEFAULT_CFG)
            cfg.update(stored)
            cfg.setdefault("level_filters", {})
            cfg.setdefault("tag_filters",   {})
            return cfg
        except Exception:
            pass
    return dict(_DEFAULT_CFG)


def _save_cfg(cfg: dict) -> None:
    with CONFIG_PATH.open("w") as fh:
        json.dump(cfg, fh, indent=2)


# ─── Serial reader (background thread) ───────────────────────────────────────
class SerialReader:
    def __init__(self, rx_q: queue.Queue) -> None:
        self._q       = rx_q
        self._port    = None
        self._running = False

    @property
    def connected(self) -> bool:
        return self._running and bool(self._port) and self._port.is_open

    def connect(self, port: str, baud: int) -> bool:
        try:
            self._port    = serial.Serial(port, baudrate=baud, timeout=0.1)
            self._running = True
            threading.Thread(target=self._loop, daemon=True).start()
            return True
        except (serial.SerialException, OSError) as exc:
            self._q.put({"_disconnect": True, "_err": str(exc)})
            return False

    def disconnect(self) -> None:
        self._running = False
        try:
            if self._port:
                self._port.close()
        except Exception:
            pass
        self._port = None

    def _loop(self) -> None:
        buf = b""
        while self._running:
            try:
                data = self._port.read(512)
                if data:
                    buf += data
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        text = line.rstrip(b"\r").decode("utf-8", errors="replace")
                        self._q.put({"_line": text})
            except (serial.SerialException, OSError) as exc:
                # Covers both pyserial's own exception and raw OS errors
                # (e.g. errno 6 ENXIO on macOS when a USB adapter resets).
                self._q.put({"_disconnect": True,
                             "_err": f"Serial error: {exc}"})
                self._running = False
                return
            except Exception as exc:                     # safety net — never die silently
                self._q.put({"_disconnect": True,
                             "_err": f"Unexpected error: {exc}"})
                self._running = False
                return


# ─── Main application ─────────────────────────────────────────────────────────
class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("FERP Serial Log Viewer")
        self.configure(bg=BG)
        self.geometry("1300x760")
        self.minsize(900, 520)

        self._cfg     = _load_cfg()
        self._rx_q    = queue.Queue()
        self._reader  = SerialReader(self._rx_q)

        self._level_vars: dict[str, tk.BooleanVar] = {}
        self._tag_vars:   dict[str, tk.BooleanVar] = {}
        self._history:    list[dict]                = []

        self._build_ui()
        self._restore_state()
        self._refresh_ports()
        self._poll()

    # ══════════════════════════════════════════════════════════════════════════
    # UI construction
    # ══════════════════════════════════════════════════════════════════════════

    def _build_ui(self) -> None:
        self._build_toolbar()

        pane = tk.PanedWindow(self, orient=tk.HORIZONTAL, bg=BG,
                              sashwidth=5, sashrelief=tk.FLAT)
        pane.pack(fill=tk.BOTH, expand=True, padx=4, pady=(2, 4))

        sidebar = self._build_sidebar(pane)
        logpane = self._build_log_pane(pane)
        pane.add(sidebar,  minsize=160)
        pane.add(logpane,  minsize=500)
        pane.paneconfigure(sidebar, width=215)

    # ── Toolbar ───────────────────────────────────────────────────────────────
    def _build_toolbar(self) -> None:
        bar = tk.Frame(self, bg=BG2, pady=5)
        bar.pack(fill=tk.X, padx=4, pady=(4, 0))

        # Port label + combobox + refresh
        tk.Label(bar, text="Port", bg=BG2, fg=DIM,
                 font=("Menlo", 10)).pack(side=tk.LEFT, padx=(8, 3))
        self._port_var = tk.StringVar()
        self._port_cb  = ttk.Combobox(bar, textvariable=self._port_var,
                                      width=26, state="readonly",
                                      font=("Menlo", 10))
        self._port_cb.pack(side=tk.LEFT, padx=(0, 1))
        tk.Button(bar, text="⟳", bg=BG2, fg=DIM, relief=tk.FLAT,
                  font=("Menlo", 13), cursor="hand2",
                  command=self._refresh_ports).pack(side=tk.LEFT)

        # Baud
        tk.Label(bar, text="Baud", bg=BG2, fg=DIM,
                 font=("Menlo", 10)).pack(side=tk.LEFT, padx=(16, 3))
        self._baud_var = tk.StringVar(value=str(self._cfg.get("baudrate", 115200)))
        tk.Entry(bar, textvariable=self._baud_var, width=9,
                 bg=BG3, fg=FG, insertbackground=FG,
                 font=("Menlo", 10), relief=tk.FLAT).pack(side=tk.LEFT)

        # Connect / Disconnect
        self._btn_conn = tk.Button(
            bar, text="Connect", bg=GREEN, fg="#1e1e2e",
            font=("Menlo", 10, "bold"), relief=tk.FLAT, cursor="hand2",
            command=self._on_connect)
        self._btn_conn.pack(side=tk.LEFT, padx=(16, 3))

        self._btn_disc = tk.Button(
            bar, text="Disconnect", bg=BG3, fg=FG,
            font=("Menlo", 10), relief=tk.FLAT, cursor="hand2",
            state=tk.DISABLED, command=self._on_disconnect)
        self._btn_disc.pack(side=tk.LEFT, padx=(0, 3))

        # Status
        self._status_var = tk.StringVar(value="Disconnected")
        self._status_lbl = tk.Label(bar, textvariable=self._status_var,
                                    bg=BG2, fg=DIM, font=("Menlo", 10))
        self._status_lbl.pack(side=tk.LEFT, padx=14)

        # Clear All (right-aligned)
        tk.Button(bar, text="Clear All", bg=BG3, fg=FG,
                  font=("Menlo", 10), relief=tk.FLAT, cursor="hand2",
                  command=self._clear_all).pack(side=tk.RIGHT, padx=8)

    # ── Filter sidebar ────────────────────────────────────────────────────────
    def _build_sidebar(self, parent) -> tk.Frame:
        frame = tk.Frame(parent, bg=BG2)

        # ── Level section ─────────────────────────────────────────────────────
        tk.Label(frame, text="LEVEL", bg=BG2, fg=DIM,
                 font=("Menlo", 9, "bold")).pack(anchor=tk.W, padx=10, pady=(10, 2))

        for lvl in ("ERR", "DEB", "WAR", "INF", "PLAIN"):
            var = tk.BooleanVar(value=self._cfg["level_filters"].get(lvl, True))
            self._level_vars[lvl] = var
            col = LEVEL_COLOR[lvl]
            tk.Checkbutton(
                frame, text=lvl, variable=var,
                bg=BG2, fg=col, selectcolor=BG3,
                activebackground=BG2, activeforeground=col,
                font=("Menlo", 10),
            ).pack(anchor=tk.W, padx=22)

        ttk.Separator(frame, orient=tk.HORIZONTAL).pack(
            fill=tk.X, padx=6, pady=(10, 4))

        # ── Tag section ───────────────────────────────────────────────────────
        tag_hdr = tk.Frame(frame, bg=BG2)
        tag_hdr.pack(fill=tk.X, padx=10, pady=(0, 4))
        tk.Label(tag_hdr, text="LOG TAG", bg=BG2, fg=DIM,
                 font=("Menlo", 9, "bold")).pack(side=tk.LEFT)
        # All / None quick buttons
        for label, val in (("All", True), ("None", False)):
            tk.Button(
                tag_hdr, text=label, bg=BG3, fg=FG, relief=tk.FLAT,
                font=("Menlo", 8), cursor="hand2",
                command=lambda v=val: self._set_all_tags(v),
            ).pack(side=tk.RIGHT, padx=1)

        # Scrollable canvas so tag list can grow without limit
        canvas = tk.Canvas(frame, bg=BG2, highlightthickness=0)
        vsb    = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=canvas.yview)
        self._tag_inner = tk.Frame(canvas, bg=BG2)

        self._tag_inner.bind(
            "<Configure>",
            lambda _e: canvas.configure(scrollregion=canvas.bbox("all")),
        )
        canvas.create_window((0, 0), window=self._tag_inner, anchor=tk.NW)
        canvas.configure(yscrollcommand=vsb.set)

        vsb.pack(side=tk.RIGHT, fill=tk.Y, padx=(0, 2))
        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(10, 0))

        def _wheel(event):
            canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

        canvas.bind("<MouseWheel>",      _wheel)
        self._tag_inner.bind("<MouseWheel>", _wheel)

        return frame

    # ── Log pane (notebook with two tabs) ─────────────────────────────────────
    def _build_log_pane(self, parent) -> tk.Frame:
        frame = tk.Frame(parent, bg=BG)

        sty = ttk.Style()
        sty.theme_use("default")
        sty.configure("TNotebook", background=BG, borderwidth=0)
        sty.configure("TNotebook.Tab", background=BG3, foreground=FG,
                       padding=[12, 4], font=("Menlo", 10))
        sty.map("TNotebook.Tab",
                background=[("selected", BG)],
                foreground=[("selected", FG)])

        self._nb = ttk.Notebook(frame)
        self._nb.pack(fill=tk.BOTH, expand=True)

        # ── Tab 1: Live Log ───────────────────────────────────────────────────
        live_f = tk.Frame(self._nb, bg=BG)
        self._nb.add(live_f, text="  Live Log  ")
        self._live_txt = self._make_text(live_f)

        # ── Tab 2: Filtered ───────────────────────────────────────────────────
        filt_f = tk.Frame(self._nb, bg=BG)
        self._nb.add(filt_f, text="  Filtered  ")

        # Filtered tab toolbar
        ftbar = tk.Frame(filt_f, bg=BG2, pady=3)
        ftbar.pack(fill=tk.X)
        self._filt_count = tk.StringVar(value="0 matching lines")
        tk.Label(ftbar, textvariable=self._filt_count,
                 bg=BG2, fg=DIM, font=("Menlo", 9)).pack(side=tk.LEFT, padx=8)
        tk.Button(ftbar, text="Clear", bg=BG3, fg=FG,
                  font=("Menlo", 10), relief=tk.FLAT, cursor="hand2",
                  command=self._clear_filtered).pack(side=tk.RIGHT, padx=4)
        tk.Button(ftbar, text="⟳  Refresh", bg=BLUE, fg="#1e1e2e",
                  font=("Menlo", 10, "bold"), relief=tk.FLAT, cursor="hand2",
                  command=self._refresh_filtered).pack(side=tk.RIGHT, padx=4)

        self._filt_txt = self._make_text(filt_f)

        return frame

    def _make_text(self, parent: tk.Frame) -> scrolledtext.ScrolledText:
        """Create a colour-tagged, read-only scrolled text widget."""
        txt = scrolledtext.ScrolledText(
            parent, bg=BG, fg=FG, font=("Menlo", 10),
            wrap=tk.NONE, state=tk.DISABLED,
            insertbackground=FG, relief=tk.FLAT,
            selectbackground=BG3, pady=2)
        txt.pack(fill=tk.BOTH, expand=True)

        # Horizontal scrollbar
        hbar = ttk.Scrollbar(parent, orient=tk.HORIZONTAL, command=txt.xview)
        hbar.pack(fill=tk.X)
        txt.configure(xscrollcommand=hbar.set)

        # Colour tags
        txt.tag_configure("hdr",      foreground=DIM)
        txt.tag_configure("tag_part", foreground=MAUVE)
        for lvl, col in LEVEL_COLOR.items():
            txt.tag_configure(f"lvl_{lvl}", foreground=col)

        return txt

    # ══════════════════════════════════════════════════════════════════════════
    # State / Config
    # ══════════════════════════════════════════════════════════════════════════

    def _restore_state(self) -> None:
        self._port_var.set(self._cfg.get("port", ""))
        self._baud_var.set(str(self._cfg.get("baudrate", 115200)))
        for lvl, var in self._level_vars.items():
            var.set(self._cfg["level_filters"].get(lvl, True))
        for tag, enabled in self._cfg.get("tag_filters", {}).items():
            self._ensure_tag(tag, default=enabled)

    def _save_state(self) -> None:
        self._cfg["port"]          = self._port_var.get()
        self._cfg["baudrate"]      = self._safe_baud()
        self._cfg["level_filters"] = {k: v.get() for k, v in self._level_vars.items()}
        self._cfg["tag_filters"]   = {k: v.get() for k, v in self._tag_vars.items()}
        _save_cfg(self._cfg)

    def _safe_baud(self) -> int:
        try:
            return int(self._baud_var.get())
        except ValueError:
            return 115200

    # ══════════════════════════════════════════════════════════════════════════
    # Port management
    # ══════════════════════════════════════════════════════════════════════════

    def _refresh_ports(self) -> None:
        ports = sorted(p.device for p in serial.tools.list_ports.comports())
        self._port_cb["values"] = ports
        cur = self._port_var.get()
        if cur in ports:
            self._port_var.set(cur)
        elif ports:
            self._port_var.set(ports[0])

    # ══════════════════════════════════════════════════════════════════════════
    # Connect / Disconnect
    # ══════════════════════════════════════════════════════════════════════════

    def _on_connect(self) -> None:
        port = self._port_var.get().strip()
        if not port:
            self._set_status("Select a port first", YELLOW)
            return
        baud = self._safe_baud()
        self._save_state()          # persist port + baud before connecting
        if self._reader.connect(port, baud):
            self._btn_conn.config(state=tk.DISABLED)
            self._btn_disc.config(state=tk.NORMAL)
            self._set_status(f"● {port}  @{baud}", GREEN)
        else:
            self._set_status("Connection failed", RED)

    def _on_disconnect(self) -> None:
        self._reader.disconnect()
        self._btn_conn.config(state=tk.NORMAL)
        self._btn_disc.config(state=tk.DISABLED)
        self._set_status("Disconnected", DIM)

    def _set_status(self, msg: str, color: str = DIM) -> None:
        self._status_var.set(msg)
        self._status_lbl.config(fg=color)

    # ══════════════════════════════════════════════════════════════════════════
    # Tag checkbox management
    # ══════════════════════════════════════════════════════════════════════════

    def _ensure_tag(self, tag: str, default: bool = True) -> tk.BooleanVar:
        """Return existing BooleanVar for tag, or create a new checkbox."""
        if tag in self._tag_vars:
            return self._tag_vars[tag]
        enabled = self._cfg.get("tag_filters", {}).get(tag, default)
        var = tk.BooleanVar(value=enabled)
        self._tag_vars[tag] = var
        tk.Checkbutton(
            self._tag_inner, text=tag, variable=var,
            bg=BG2, fg=TEAL, selectcolor=BG3,
            activebackground=BG2, activeforeground=TEAL,
            font=("Menlo", 9),
        ).pack(anchor=tk.W, padx=6, pady=1)
        return var

    def _set_all_tags(self, value: bool) -> None:
        for var in self._tag_vars.values():
            var.set(value)

    # ══════════════════════════════════════════════════════════════════════════
    # Filter predicate
    # ══════════════════════════════════════════════════════════════════════════

    def _passes(self, entry: dict) -> bool:
        level = entry["level"]
        tag   = entry["tag"]
        if not self._level_vars.get(level, tk.BooleanVar(value=True)).get():
            return False
        if tag and tag in self._tag_vars and not self._tag_vars[tag].get():
            return False
        return True

    # ══════════════════════════════════════════════════════════════════════════
    # Log text rendering
    # ══════════════════════════════════════════════════════════════════════════

    # Used to split a plain-text log line into colourable segments
    _RENDER_RE = re.compile(
        r"^(?P<hdr>\s*\d+\s+\d+\s+)"        # timestamp + index
        r"(?P<lvl>ERR|DEB|WAR|INF)"          # level
        r"(?P<gap>\s+)"                       # whitespace
        r"(?P<tag_part>\[[^\]]+\]|\S+)"      # [Module] or RAW_TAG
        r"(?P<rest>.*)",                      # rest of line
        re.DOTALL,
    )

    def _append(self, widget: scrolledtext.ScrolledText, entry: dict) -> None:
        plain = entry["plain_text"]
        lvl_t = f"lvl_{entry['level']}"
        m = self._RENDER_RE.match(plain)
        if m:
            widget.insert(tk.END, m.group("hdr"),           "hdr")
            widget.insert(tk.END, m.group("lvl"),            lvl_t)
            widget.insert(tk.END, m.group("gap"),            "")
            widget.insert(tk.END, m.group("tag_part"),       "tag_part")
            widget.insert(tk.END, m.group("rest") + "\n",   lvl_t)
        else:
            widget.insert(tk.END, plain + "\n",              lvl_t)

    # ══════════════════════════════════════════════════════════════════════════
    # Filtered view actions
    # ══════════════════════════════════════════════════════════════════════════

    def _refresh_filtered(self) -> None:
        self._filt_txt.config(state=tk.NORMAL)
        self._filt_txt.delete("1.0", tk.END)
        count = 0
        for entry in self._history:
            if self._passes(entry):
                self._append(self._filt_txt, entry)
                count += 1
        self._filt_txt.config(state=tk.DISABLED)
        self._filt_txt.see(tk.END)
        self._filt_count.set(f"{count} matching lines")

    def _clear_filtered(self) -> None:
        self._filt_txt.config(state=tk.NORMAL)
        self._filt_txt.delete("1.0", tk.END)
        self._filt_txt.config(state=tk.DISABLED)
        self._filt_count.set("0 matching lines")

    def _clear_all(self) -> None:
        self._history.clear()
        for w in (self._live_txt, self._filt_txt):
            w.config(state=tk.NORMAL)
            w.delete("1.0", tk.END)
            w.config(state=tk.DISABLED)
        self._filt_count.set("0 matching lines")

    # ══════════════════════════════════════════════════════════════════════════
    # Background queue poll  (runs on the UI thread via after())
    # ══════════════════════════════════════════════════════════════════════════

    def _poll(self) -> None:
        processed = 0
        while processed < BATCH:
            try:
                item = self._rx_q.get_nowait()
            except queue.Empty:
                break
            processed += 1

            if "_disconnect" in item:
                self._on_disconnect()
                msg = item.get("_err", "Device disconnected")
                self._set_status(f"⚠  {msg}", YELLOW)
                break

            raw   = item.get("_line", "")
            entry = parse_line(raw)

            # Auto-register previously unseen tags
            if entry["tag"]:
                self._ensure_tag(entry["tag"])

            self._history.append(entry)

            # Always write to the live log
            self._live_txt.config(state=tk.NORMAL)
            self._append(self._live_txt, entry)

            # Trim oldest lines if live log grows too large
            n_lines = int(self._live_txt.index(tk.END).split(".")[0]) - 1
            if n_lines > MAX_LIVE_LINES + TRIM_CHUNK:
                self._live_txt.delete("1.0", f"{TRIM_CHUNK + 1}.0")

            self._live_txt.config(state=tk.DISABLED)
            self._live_txt.see(tk.END)

        self.after(POLL_MS, self._poll)

    # ══════════════════════════════════════════════════════════════════════════
    # Cleanup
    # ══════════════════════════════════════════════════════════════════════════

    def destroy(self) -> None:
        self._save_state()
        self._reader.disconnect()
        super().destroy()


# ─── Entry point ──────────────────────────────────────────────────────────────
if __name__ == "__main__":
    App().mainloop()
