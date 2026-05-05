"""
spiffs_widget.py — SPIFFS file explorer tab for the Simulator UI.

Browses the simulator's SPIFFS emulation directory (path from sim_paths.json).

Displays the folder tree in a Treeview on the left; clicking a file
shows its content in the right pane. JSON files are pretty-printed.
A "Refresh" button rescans the directory at any time.
"""

import json
import os
import struct
import datetime
import tkinter as tk
from tkinter import ttk
from .paths import sim_paths

_BG      = "#1e1e2e"
_BG_DARK = "#11111b"
_BG_MID  = "#181825"
_FG      = "#cdd6f4"
_FG_DIM  = "#6c7086"
_ACCENT  = "#89b4fa"
_SEL_BG  = "#313244"
_MONO    = ("Menlo", 10)
_MONO_SM = ("Menlo", 9)

# Resolved from sim_paths.json — edit that file to change this path.
_SPIFFS_ROOT = sim_paths["spiffs_root"]


class SpiffsWidget(tk.Frame):
    """File explorer for the simulator SPIFFS emulation directory."""

    def __init__(self, parent, **kwargs):
        super().__init__(parent, bg=_BG, **kwargs)
        self._node_paths: dict[str, str] = {}   # tree item id → abs path
        self._build_ui()
        self.refresh()

    # ── UI construction ───────────────────────────────────────────────────────

    def _build_ui(self):
        # Toolbar
        toolbar = tk.Frame(self, bg=_BG_DARK, pady=4)
        toolbar.pack(fill=tk.X)

        tk.Label(toolbar, text="SPIFFS Explorer",
                 bg=_BG_DARK, fg=_FG,
                 font=("Menlo", 11, "bold")).pack(side=tk.LEFT, padx=8)

        tk.Label(toolbar, text=_SPIFFS_ROOT,
                 bg=_BG_DARK, fg=_FG_DIM,
                 font=_MONO_SM).pack(side=tk.LEFT, padx=4)

        tk.Button(toolbar, text="⟳  Refresh",
                  bg="#313244", fg=_FG,
                  activebackground=_ACCENT,
                  font=_MONO_SM, relief=tk.FLAT, padx=6,
                  command=self.refresh).pack(side=tk.RIGHT, padx=8)

        # Main pane (tree | content)
        pane = tk.PanedWindow(self, orient=tk.HORIZONTAL,
                               bg=_BG, sashwidth=4)
        pane.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        # ── Left: tree ────────────────────────────────────────────────────────
        tree_frame = tk.Frame(pane, bg=_BG)
        pane.add(tree_frame, minsize=200)

        style = ttk.Style()
        style.configure("Spiffs.Treeview",
                         background=_BG_MID, foreground=_FG,
                         fieldbackground=_BG_MID, rowheight=22)
        style.configure("Spiffs.Treeview.Heading",
                         background=_BG_DARK, foreground=_FG)
        style.map("Spiffs.Treeview",
                   background=[("selected", _SEL_BG)],
                   foreground=[("selected", _FG)])

        self._tree = ttk.Treeview(tree_frame, style="Spiffs.Treeview",
                                   show="tree", selectmode="browse")
        tree_vsb = tk.Scrollbar(tree_frame, orient=tk.VERTICAL,
                                 command=self._tree.yview)
        self._tree.configure(yscrollcommand=tree_vsb.set)
        tree_vsb.pack(side=tk.RIGHT, fill=tk.Y)
        self._tree.pack(fill=tk.BOTH, expand=True)
        self._tree.bind("<<TreeviewSelect>>", self._on_select)

        # ── Right: content viewer ─────────────────────────────────────────────
        right = tk.Frame(pane, bg=_BG)
        pane.add(right, minsize=300)

        self._file_label = tk.Label(right, text="(no file selected)",
                                     bg=_BG_DARK, fg=_FG_DIM,
                                     font=_MONO_SM, anchor=tk.W,
                                     padx=6, pady=3)
        self._file_label.pack(fill=tk.X)

        txt_frame = tk.Frame(right, bg=_BG)
        txt_frame.pack(fill=tk.BOTH, expand=True)

        self._text = tk.Text(txt_frame,
                              bg=_BG_MID, fg=_FG, insertbackground=_FG,
                              font=_MONO, relief=tk.FLAT,
                              wrap=tk.NONE, state=tk.DISABLED)
        vsb = tk.Scrollbar(txt_frame, orient=tk.VERTICAL,
                            command=self._text.yview)
        hsb = tk.Scrollbar(right, orient=tk.HORIZONTAL,
                            command=self._text.xview)
        self._text.configure(yscrollcommand=vsb.set,
                              xscrollcommand=hsb.set)
        hsb.pack(fill=tk.X, side=tk.BOTTOM)
        vsb.pack(side=tk.RIGHT, fill=tk.Y)
        self._text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

    # ── Tree population ───────────────────────────────────────────────────────

    def refresh(self):
        """Re-scan the SPIFFS root and rebuild the tree."""
        self._tree.delete(*self._tree.get_children())
        self._node_paths.clear()

        if not os.path.isdir(_SPIFFS_ROOT):
            self._tree.insert("", tk.END,
                               text="⚠  directory not found: " + _SPIFFS_ROOT)
            return

        self._populate_tree("", _SPIFFS_ROOT)

    def _populate_tree(self, parent_id: str, dir_path: str):
        try:
            entries = sorted(
                os.scandir(dir_path),
                key=lambda e: (not e.is_dir(), e.name.lower()))
        except PermissionError:
            return

        for entry in entries:
            icon = "📁 " if entry.is_dir() else "📄 "
            node = self._tree.insert(
                parent_id, tk.END,
                text=icon + entry.name,
                open=True)
            self._node_paths[node] = entry.path
            if entry.is_dir():
                self._populate_tree(node, entry.path)

    # ── Selection handler ─────────────────────────────────────────────────────

    def _on_select(self, _event):
        sel = self._tree.selection()
        if not sel:
            return
        path = self._node_paths.get(sel[0])
        if path is None or os.path.isdir(path):
            return
        self._show_file(path)

    def _show_file(self, path: str):
        rel = os.path.relpath(path, _SPIFFS_ROOT)
        self._file_label.config(text=rel, fg=_FG)

        try:
            if path.endswith(".bin"):
                raw = self._format_bin(path)
            else:
                with open(path, "r", encoding="utf-8", errors="replace") as fh:
                    raw = fh.read()
                if path.endswith(".json"):
                    try:
                        raw = json.dumps(json.loads(raw), indent=2)
                    except json.JSONDecodeError:
                        pass
        except OSError as e:
            raw = f"Error reading file:\n{e}"

        self._text.config(state=tk.NORMAL)
        self._text.delete("1.0", tk.END)
        self._text.insert("1.0", raw)
        self._text.config(state=tk.DISABLED)

    @staticmethod
    def _format_bin(path: str) -> str:
        """Render a binary file as a hex dump, with special decoding for known formats."""
        with open(path, "rb") as fh:
            data = fh.read()

        lines = []

        # ── Known formats ──────────────────────────────────────────────────────
        name = os.path.basename(path)
        if name == "timemgr.bin" and len(data) >= 8:
            epoch = struct.unpack_from("<q", data)[0]   # int64_t little-endian
            try:
                dt = datetime.datetime.utcfromtimestamp(epoch)
                human = dt.strftime("%Y-%m-%d %H:%M:%S UTC")
            except (OSError, OverflowError, ValueError):
                human = "(invalid)"
            lines.append(f"timemgr backup — epoch: {epoch}  ({human})")
            lines.append("")

        # ── Generic hex dump ───────────────────────────────────────────────────
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            hex_part  = " ".join(f"{b:02x}" for b in chunk)
            ascii_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
            lines.append(f"{i:08x}  {hex_part:<47}  |{ascii_part}|")

        return "\n".join(lines) if lines else "(empty)"
