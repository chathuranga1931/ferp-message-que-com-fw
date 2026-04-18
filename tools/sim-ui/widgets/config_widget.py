"""
config_widget.py — Config JSON viewer tab for the Simulator UI.

Reads the SPIFFS-emulated DeviceConfigs.json directly from the
simulator's source directory and displays it as an editable table.

Default file path (relative to this file):
    src/product/ferp-com-simulator/SPIFFS/spiffs/Configs/DeviceConfigs.json
"""

import json
import os
import tkinter as tk
from tkinter import ttk, messagebox


_DARK_BG  = "#1e1e2e"
_DARK_FG  = "#cdd6f4"
_ROW_ODD  = "#181825"
_ROW_EVEN = "#1e1e2e"
_ACCENT   = "#89b4fa"
_HEADING  = "#313244"
_MONO     = ("Menlo", 10)
_MONO_SM  = ("Menlo", 9)

# Path relative to this file:
#   tools/sim-ui/widgets/  →  ../../../src/product/ferp-com-simulator/SPIFFS/spiffs/Configs/
_DEFAULT_PATH = os.path.normpath(os.path.join(
    os.path.dirname(__file__),                          # tools/sim-ui/widgets/
    "..", "..", "..",                                   # repo root
    "src", "product", "ferp-com-simulator",
    "SPIFFS", "spiffs", "Configs", "DeviceConfigs.json"
))


class ConfigWidget(tk.Frame):
    """Reads Configs/DeviceConfigs.json from the SPIFFS emulator directory
    and shows each key-value pair in a two-column table.

    A "Refresh" button re-reads the file.
    The path entry lets the user point to a different file.
    """

    def __init__(self, parent, **kwargs):
        super().__init__(parent, bg=_DARK_BG, **kwargs)
        self._build_ui()
        self.refresh()

    # ── UI construction ──────────────────────────────────────────────────────

    def _build_ui(self):
        # ── Path row ─────────────────────────────────────────────────────────
        path_row = tk.Frame(self, bg=_DARK_BG)
        path_row.pack(fill=tk.X, padx=6, pady=(6, 2))

        tk.Label(path_row, text="File:", bg=_DARK_BG, fg=_DARK_FG,
                 font=_MONO_SM).pack(side=tk.LEFT)

        self._path_var = tk.StringVar(value=_DEFAULT_PATH)
        path_entry = tk.Entry(path_row, textvariable=self._path_var,
                              bg="#313244", fg=_DARK_FG, insertbackground=_DARK_FG,
                              font=_MONO_SM, relief=tk.FLAT, bd=4)
        path_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(4, 4))

        self._refresh_btn = tk.Button(
            path_row, text="⟳  Refresh",
            command=self.refresh,
            bg="#313244", fg=_ACCENT,
            activebackground="#45475a", activeforeground=_ACCENT,
            font=_MONO, relief=tk.FLAT, padx=8, pady=2)
        self._refresh_btn.pack(side=tk.LEFT)

        # ── Status label ─────────────────────────────────────────────────────
        self._status = tk.Label(self, text="", bg=_DARK_BG, fg="#585b70",
                                font=_MONO_SM, anchor=tk.W)
        self._status.pack(fill=tk.X, padx=6)

        # ── Treeview ─────────────────────────────────────────────────────────
        tree_frame = tk.Frame(self, bg=_DARK_BG)
        tree_frame.pack(fill=tk.BOTH, expand=True, padx=6, pady=4)

        style = ttk.Style()
        style.theme_use("default")
        style.configure("Config.Treeview",
                        background=_ROW_ODD, foreground=_DARK_FG,
                        fieldbackground=_ROW_ODD, rowheight=22,
                        font=_MONO_SM)
        style.configure("Config.Treeview.Heading",
                        background=_HEADING, foreground=_ACCENT,
                        font=("Menlo", 10, "bold"), relief=tk.FLAT)
        style.map("Config.Treeview",
                  background=[("selected", "#45475a")],
                  foreground=[("selected", _DARK_FG)])

        self._tree = ttk.Treeview(tree_frame,
                                  columns=("key", "value"),
                                  show="headings",
                                  style="Config.Treeview",
                                  selectmode="browse")
        self._tree.heading("key",   text="Key",   anchor=tk.W)
        self._tree.heading("value", text="Value", anchor=tk.W)
        self._tree.column("key",   width=200, minwidth=120, stretch=False)
        self._tree.column("value", width=400, minwidth=200, stretch=True)

        vsb = ttk.Scrollbar(tree_frame, orient=tk.VERTICAL,
                            command=self._tree.yview)
        self._tree.configure(yscrollcommand=vsb.set)

        self._tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        vsb.pack(side=tk.RIGHT, fill=tk.Y)

        # Alternating row colours
        self._tree.tag_configure("odd",  background=_ROW_ODD)
        self._tree.tag_configure("even", background=_ROW_EVEN)

    # ── Public API ────────────────────────────────────────────────────────────

    def refresh(self):
        """Re-read the JSON file and repopulate the table."""
        path = self._path_var.get().strip()
        self._load_file(path)

    # ── Internals ─────────────────────────────────────────────────────────────

    def _load_file(self, path: str):
        self._tree.delete(*self._tree.get_children())

        if not os.path.isfile(path):
            self._status.config(
                text=f"⚠  File not found: {path}",
                fg="#f38ba8")
            return

        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except json.JSONDecodeError as e:
            self._status.config(text=f"❌ JSON parse error: {e}", fg="#f38ba8")
            return
        except OSError as e:
            self._status.config(text=f"❌ Read error: {e}", fg="#f38ba8")
            return

        if not isinstance(data, dict):
            self._status.config(text="⚠  Expected a JSON object at the top level",
                                fg="#f9e2af")
            return

        self._populate(data)
        self._status.config(
            text=f"✅ Loaded {len(data)} field(s) from {os.path.basename(path)}",
            fg="#a6e3a1")

    def _populate(self, data: dict, prefix: str = "", row_idx: list = None):
        """Recursively insert rows; nested objects are indented."""
        if row_idx is None:
            row_idx = [0]

        for key, value in data.items():
            display_key = f"{prefix}{key}" if prefix else key
            tag = "odd" if row_idx[0] % 2 == 0 else "even"

            if isinstance(value, dict):
                # Insert a section header row
                self._tree.insert("", tk.END, values=(display_key, "{…}"),
                                  tags=(tag,))
                row_idx[0] += 1
                self._populate(value, prefix=display_key + ".", row_idx=row_idx)
            else:
                self._tree.insert("", tk.END,
                                  values=(display_key, str(value)),
                                  tags=(tag,))
                row_idx[0] += 1
