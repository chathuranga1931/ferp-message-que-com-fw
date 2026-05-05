"""
sdcard_widget.py — SD Card file explorer tab for the Simulator UI.

Browses the simulator's SD card emulation directory (path from sim_paths.json).

Displays the folder tree in a Treeview on the left; clicking a file
shows its content in the right pane with file size info.
JSON / text files are displayed as-is; binary files show a hex preview.
Buttons: Refresh, New Folder, Delete (file or empty dir).
"""

import json
import os
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
from .paths import sim_paths

_BG      = "#1e1e2e"
_BG_DARK = "#11111b"
_BG_MID  = "#181825"
_FG      = "#cdd6f4"
_FG_DIM  = "#6c7086"
_ACCENT  = "#a6e3a1"   # green — distinct from SPIFFS blue
_SEL_BG  = "#313244"
_MONO    = ("Menlo", 10)
_MONO_SM = ("Menlo", 9)

# Resolved from sim_paths.json — edit that file to change this path.
_SDCARD_ROOT = sim_paths["sdcard_root"]

_TEXT_EXTENSIONS = {
    ".txt", ".log", ".csv", ".json", ".xml", ".html",
    ".cfg", ".ini", ".md", ".py", ".cpp", ".h", ".c",
}


def _fmt_size(n: int) -> str:
    if n < 1024:
        return f"{n} B"
    elif n < 1024 * 1024:
        return f"{n / 1024:.1f} KB"
    else:
        return f"{n / (1024 * 1024):.1f} MB"


class SdCardWidget(tk.Frame):
    """File explorer for the simulator SD card emulation directory."""

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

        tk.Label(toolbar, text="SD Card Explorer",
                 bg=_BG_DARK, fg=_FG,
                 font=("Menlo", 11, "bold")).pack(side=tk.LEFT, padx=8)

        tk.Label(toolbar, text=_SDCARD_ROOT,
                 bg=_BG_DARK, fg=_FG_DIM,
                 font=_MONO_SM).pack(side=tk.LEFT, padx=4)

        # Right-side buttons
        for label, cmd in [
            ("⟳  Refresh",    self.refresh),
            ("📁  New Folder", self._new_folder),
            ("🗑  Delete",     self._delete_selected),
        ]:
            tk.Button(toolbar, text=label,
                      bg="#313244", fg=_FG,
                      activebackground=_ACCENT,
                      font=_MONO_SM, relief=tk.FLAT, padx=6,
                      command=cmd).pack(side=tk.RIGHT, padx=4)

        # Status bar
        self._status = tk.Label(self, text="", bg=_BG_DARK, fg=_FG_DIM,
                                 font=_MONO_SM, anchor=tk.W, padx=6, pady=2)
        self._status.pack(fill=tk.X, side=tk.BOTTOM)

        # Main pane (tree | content)
        pane = tk.PanedWindow(self, orient=tk.HORIZONTAL,
                               bg=_BG, sashwidth=4)
        pane.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)

        # ── Left: tree ────────────────────────────────────────────────────────
        tree_frame = tk.Frame(pane, bg=_BG)
        pane.add(tree_frame, minsize=220)

        style = ttk.Style()
        style.configure("SD.Treeview",
                         background=_BG_MID, foreground=_FG,
                         fieldbackground=_BG_MID, rowheight=22)
        style.configure("SD.Treeview.Heading",
                         background=_BG_DARK, foreground=_FG)
        style.map("SD.Treeview",
                   background=[("selected", _SEL_BG)],
                   foreground=[("selected", _FG)])

        self._tree = ttk.Treeview(tree_frame, style="SD.Treeview",
                                   columns=("size",),
                                   show="tree headings",
                                   selectmode="browse")
        self._tree.heading("#0",    text="Name",   anchor=tk.W)
        self._tree.heading("size",  text="Size",   anchor=tk.E)
        self._tree.column("#0",    stretch=True,  minwidth=120)
        self._tree.column("size",  width=70, stretch=False, anchor=tk.E)

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
        """Re-scan the SDCARD root and rebuild the tree."""
        self._tree.delete(*self._tree.get_children())
        self._node_paths.clear()
        self._clear_content()

        if not os.path.isdir(_SDCARD_ROOT):
            self._tree.insert("", tk.END, text="⚠  not found: " + _SDCARD_ROOT,
                               values=("",))
            return

        total = self._count_files(_SDCARD_ROOT)
        self._populate_tree("", _SDCARD_ROOT)
        self._status.config(text=f"Root: {_SDCARD_ROOT}   |   {total} file(s)")

    def _count_files(self, path: str) -> int:
        count = 0
        for entry in os.scandir(path):
            if entry.name.startswith('.'): continue
            if entry.is_dir():
                count += self._count_files(entry.path)
            else:
                count += 1
        return count

    def _populate_tree(self, parent_id: str, dir_path: str):
        try:
            entries = sorted(
                os.scandir(dir_path),
                key=lambda e: (not e.is_dir(), e.name.lower()))
        except PermissionError:
            return

        for entry in entries:
            if entry.name.startswith('.'):
                continue
            if entry.is_dir():
                icon = "📁 "
                size_str = ""
            else:
                icon = "📄 "
                try:
                    size_str = _fmt_size(entry.stat().st_size)
                except OSError:
                    size_str = "?"

            node = self._tree.insert(
                parent_id, tk.END,
                text=icon + entry.name,
                values=(size_str,),
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
        if path is None:
            return
        if os.path.isdir(path):
            rel = os.path.relpath(path, _SDCARD_ROOT)
            self._file_label.config(text=f"📁  {rel}", fg=_FG_DIM)
            self._clear_content()
        else:
            self._show_file(path)

    def _show_file(self, path: str):
        rel = os.path.relpath(path, _SDCARD_ROOT)
        try:
            size = os.path.getsize(path)
            self._file_label.config(
                text=f"{rel}   ({_fmt_size(size)})", fg=_FG)
        except OSError:
            self._file_label.config(text=rel, fg=_FG)

        ext = os.path.splitext(path)[1].lower()
        try:
            if ext in _TEXT_EXTENSIONS:
                with open(path, "r", encoding="utf-8", errors="replace") as fh:
                    raw = fh.read()
                if ext == ".json":
                    try:
                        raw = json.dumps(json.loads(raw), indent=2)
                    except json.JSONDecodeError:
                        pass
            else:
                # Binary — show hex dump (first 512 bytes)
                with open(path, "rb") as fh:
                    data = fh.read(512)
                lines = []
                for i in range(0, len(data), 16):
                    chunk = data[i:i + 16]
                    hex_part = " ".join(f"{b:02x}" for b in chunk)
                    asc_part = "".join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    lines.append(f"{i:04x}  {hex_part:<47}  {asc_part}")
                raw = "\n".join(lines)
                if len(data) == 512:
                    raw += "\n… (truncated)"
        except OSError as e:
            raw = f"Error reading file:\n{e}"

        self._set_content(raw)

    # ── Toolbar actions ───────────────────────────────────────────────────────

    def _new_folder(self):
        """Create a new subdirectory inside the selected directory (or root)."""
        base = _SDCARD_ROOT
        sel = self._tree.selection()
        if sel:
            p = self._node_paths.get(sel[0])
            if p and os.path.isdir(p):
                base = p

        rel_base = os.path.relpath(base, _SDCARD_ROOT)
        name = simpledialog.askstring(
            "New Folder",
            f"Folder name inside  /{rel_base}:" if rel_base != "." else "Folder name at SD root:",
            parent=self)
        if not name:
            return
        new_path = os.path.join(base, name)
        try:
            os.makedirs(new_path, exist_ok=True)
            self.refresh()
        except OSError as e:
            messagebox.showerror("Error", f"Could not create folder:\n{e}", parent=self)

    def _delete_selected(self):
        sel = self._tree.selection()
        if not sel:
            messagebox.showinfo("Delete", "Select a file or folder first.", parent=self)
            return
        path = self._node_paths.get(sel[0])
        if not path:
            return
        rel = os.path.relpath(path, _SDCARD_ROOT)
        if not messagebox.askyesno("Delete", f"Delete  /{rel}?", parent=self):
            return
        try:
            if os.path.isdir(path):
                os.rmdir(path)
            else:
                os.unlink(path)
            self.refresh()
        except OSError as e:
            messagebox.showerror("Error", f"Delete failed:\n{e}", parent=self)

    # ── Helpers ───────────────────────────────────────────────────────────────

    def _clear_content(self):
        self._text.config(state=tk.NORMAL)
        self._text.delete("1.0", tk.END)
        self._text.config(state=tk.DISABLED)

    def _set_content(self, text: str):
        self._text.config(state=tk.NORMAL)
        self._text.delete("1.0", tk.END)
        self._text.insert("1.0", text)
        self._text.config(state=tk.DISABLED)
