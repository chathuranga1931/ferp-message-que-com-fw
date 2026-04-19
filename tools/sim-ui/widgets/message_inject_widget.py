"""
message_inject_widget.py
========================
A "Messages" tab that lets the user browse all implemented HSYS message types
and inject them directly into the firmware over TCP.

Message definitions are loaded from the  messages/  directory     # Default paths — both files live alongside sim_ui.py
    _DEFAULT_MESSAGES_DIR = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "messages",
    )
    _DEFAULT_MODULES_JSON = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "modules.json",
    )

    def __init__(self, parent, send_fn=None, messages_dir: str = None,
                 modules_json: str = None, **kwargs):
        super().__init__(parent, bg=self.BG, **kwargs)
        self._send_fn   = send_fn
        self._desc      = None
        self._fields_ui = []   # [(field_dict, tk.StringVar), …]

        path = messages_dir or self._DEFAULT_MESSAGES_DIR
        try:
            self._cat_order, self._messages = _load_messages(path)
            self._load_error = None
        except Exception as exc:
            self._cat_order  = []
            self._messages   = {}
            self._load_error = str(exc)

        mod_path = modules_json or self._DEFAULT_MODULES_JSON
        try:
            self._module_entries = _load_modules(mod_path)   # [(label, id), …]
        except Exception:
            # Fallback: just BROADCAST
            self._module_entries = [("BROADCAST  (id=0)", 0)]py:

    tools/sim-ui/
      messages/
        Buttons/
          msg_default_btn.json
          msg_printer_btn.json
        Config/
          msg_config_get.json
          ...
        Timer/
          ...

  • Each sub-folder  = one category in the tree.
  • Each .json file  = one message; filename (without .json) is the message name.
  • Category and message order follows alphabetical sort of the folder/file names.
    To control order, prefix filenames with numbers: 01_msg_foo.json.
    The numeric prefix is stripped from the display name automatically.

Individual message JSON format
-------------------------------
{
  "msg_id": "0x0900",           ← hex string or plain int
  "fields": [                   ← empty array = no-payload message
    {
      "name":    "status",      ← payload key (required)
      "label":   "Status",      ← human-readable (optional; falls back to name)
      "type":    "enum",        ← see supported types below
      "options": [              ← required for enum
        {"label": "short_press", "value": 0},
        {"label": "long_press",  "value": 1}
      ],
      "default": 0              ← optional default value
    }
  ]
}

Supported field types
---------------------
  enum    → ttk.Combobox  (options: [{label, value}, …])
  bool    → ttk.Combobox  false=0 / true=1  (shorthand for 2-option enum)
  uint8   → tk.Entry  validated  0 – 255
  uint16  → tk.Entry  validated  0 – 65535
  uint32  → tk.Entry  validated  0 – 4294967295
  int8    → tk.Entry  validated -128 – 127
  int16   → tk.Entry  validated -32768 – 32767
  int32   → tk.Entry  validated -2147483648 – 2147483647
  float   → tk.Entry  decimal
  double  → tk.Entry  decimal
  string  → tk.Entry  free text
"""

import json
import os
import re
import tkinter as tk
from tkinter import ttk

# ─────────────────────────────────────────────────────────────────────────────
# Type metadata
# ─────────────────────────────────────────────────────────────────────────────

# Signed / unsigned integer bounds  (type_name → (min, max))
_INT_BOUNDS = {
    "uint8":  (0,                0xFF),
    "uint16": (0,                0xFFFF),
    "uint32": (0,                0xFFFF_FFFF),
    "int8":   (-128,             127),
    "int16":  (-32_768,          32_767),
    "int32":  (-2_147_483_648,   2_147_483_647),
}

_INT_TYPES   = set(_INT_BOUNDS.keys())
_FLOAT_TYPES = {"float", "double"}


# ─────────────────────────────────────────────────────────────────────────────
# Module registry loader
# ─────────────────────────────────────────────────────────────────────────────

def _load_modules(modules_json_path: str) -> list:
    """
    Load modules.json and return a sorted list of (display_label, id) tuples.

    display_label  = "ModuleFoo  (id=6)"
    id             = int module ID

    The list is sorted by id, with id=0 (BROADCAST) always first.
    """
    with open(modules_json_path, "r", encoding="utf-8") as fh:
        raw = json.load(fh)

    entries = []
    for m in raw.get("modules", []):
        mid   = int(m["id"])
        name  = str(m["name"])
        label = f"{name}  (id={mid})"
        entries.append((label, mid))

    # Sort: BROADCAST (id=0) first, rest by id
    entries.sort(key=lambda x: (x[1] != 0, x[1]))
    return entries


# ─────────────────────────────────────────────────────────────────────────────
# JSON loader — scans messages/ directory tree
# ─────────────────────────────────────────────────────────────────────────────

# Strip a leading numeric sort-prefix from a filename stem: "01_foo" → "foo"
_PREFIX_RE = re.compile(r"^\d+[_\-]?")

def _strip_prefix(stem: str) -> str:
    return _PREFIX_RE.sub("", stem)


def _normalise_field(f: dict) -> dict:
    """Normalise one field dict in-place and return it."""
    field = dict(f)
    ftype = field.get("type", "string")

    # "bool" → 2-option enum
    if ftype == "bool":
        field["type"]    = "enum"
        field["options"] = [
            {"label": "false", "value": 0},
            {"label": "true",  "value": 1},
        ]

    # Normalise enum options: [{label, value}] → [(label, value)]
    if field.get("type") == "enum":
        field["options"] = [
            (str(o["label"]), o["value"])
            for o in field.get("options", [])
        ]

    # Fill in missing min/max from type defaults
    if field.get("type") in _INT_BOUNDS:
        lo, hi = _INT_BOUNDS[field["type"]]
        field.setdefault("min", lo)
        field.setdefault("max", hi)

    return field


def _load_messages(messages_dir: str) -> tuple:
    """
    Scan  messages_dir/  for category sub-folders and per-message JSON files.

    Directory layout::

        messages_dir/
          <Category>/           ← sub-folder name = category label
            <msg_name>.json     ← filename stem = message name

    Returns
    -------
    (category_order: list[str], messages: dict)

    messages shape:  {category: {msg_name: {"msg_id": int, "fields": [...]}}}
    """
    if not os.path.isdir(messages_dir):
        raise FileNotFoundError(f"messages directory not found: {messages_dir}")

    category_order: list = []
    messages: dict = {}
    errors: list = []

    # Iterate sub-folders sorted alphabetically (strip numeric prefix for display)
    raw_cats = sorted(
        d for d in os.listdir(messages_dir)
        if os.path.isdir(os.path.join(messages_dir, d)) and not d.startswith("_")
    )

    for raw_cat in raw_cats:
        cat_name = _strip_prefix(raw_cat)
        cat_dir  = os.path.join(messages_dir, raw_cat)
        category_order.append(cat_name)
        messages[cat_name] = {}

        raw_files = sorted(
            f for f in os.listdir(cat_dir)
            if f.endswith(".json") and not f.startswith("_")
        )

        for fname in raw_files:
            stem     = fname[:-5]                   # strip .json
            msg_name = _strip_prefix(stem)
            fpath    = os.path.join(cat_dir, fname)

            try:
                with open(fpath, "r", encoding="utf-8") as fh:
                    raw = json.load(fh)
            except Exception as exc:
                errors.append(f"{fpath}: {exc}")
                continue

            # msg_id may be hex string "0x0900" or plain int
            raw_id = raw.get("msg_id", 0)
            try:
                msg_id = int(raw_id, 0) if isinstance(raw_id, str) else int(raw_id)
            except (ValueError, TypeError) as exc:
                errors.append(f"{fpath}: bad msg_id — {exc}")
                continue

            fields = [_normalise_field(f) for f in raw.get("fields", [])]
            messages[cat_name][msg_name] = {"msg_id": msg_id, "fields": fields}

    if errors:
        raise ValueError("Errors loading messages:\n" + "\n".join(errors))

    return category_order, messages


# ─────────────────────────────────────────────────────────────────────────────
# Widget
# ─────────────────────────────────────────────────────────────────────────────

class MessageInjectWidget(tk.Frame):
    """
    Parameters
    ----------
    parent    : tk parent widget
    send_fn   : callable(dict) — forwards a command dict over TCP
    json_path : path to messages.json  (default: <sim-ui dir>/messages.json)
    """

    # Colour palette (Catppuccin-ish, matches rest of UI)
    BG        = "#1e1e2e"
    BG_PANEL  = "#181825"
    BG_ENTRY  = "#e8e8f0"   # light — reliable contrast on macOS ttk
    FG        = "#cdd6f4"
    FG_ENTRY  = "#1e1e2e"   # dark text on light entry/combo background
    FG_DIM    = "#6c7086"
    FG_GREEN  = "#a6e3a1"
    FG_RED    = "#f38ba8"
    FG_BLUE   = "#89b4fa"
    FG_MAUVE  = "#cba6f7"
    FONT      = ("Menlo", 11)
    FONT_BOLD = ("Menlo", 11, "bold")
    FONT_SM   = ("Menlo", 9)

    # Default paths — message JSONs live in src/app-messages/messages/,
    # modules list lives in src/app-modules/modules.json.
    # widgets/ is 4 levels deep from the repo root:
    #   <repo>/tools/sim-ui/widgets/message_inject_widget.py
    _REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))
    _DEFAULT_MESSAGES_DIR = os.path.join(
        _REPO_ROOT, "src", "app-messages", "messages"
    )
    _DEFAULT_MODULES_JSON = os.path.join(
        _REPO_ROOT, "src", "app-modules", "modules.json"
    )

    def __init__(self, parent, send_fn=None, messages_dir: str = None,
                 modules_json: str = None, **kwargs):
        super().__init__(parent, bg=self.BG, **kwargs)
        self._send_fn   = send_fn
        self._desc      = None
        self._fields_ui = []   # [(field_dict, tk.StringVar), …]

        path = messages_dir or self._DEFAULT_MESSAGES_DIR
        try:
            self._cat_order, self._messages = _load_messages(path)
            self._load_error = None
        except Exception as exc:
            self._cat_order  = []
            self._messages   = {}
            self._load_error = str(exc)

        mod_path = modules_json or self._DEFAULT_MODULES_JSON
        try:
            self._module_entries = _load_modules(mod_path)   # [(label, id), …]
        except Exception:
            self._module_entries = [("BROADCAST  (id=0)", 0)]

        self._build_ui()

    # ── UI construction ───────────────────────────────────────────────────────

    def _build_ui(self):
        style = ttk.Style(self)
        style.theme_use("default")
        style.configure(
            "MsgTree.Treeview",
            background      = self.BG_PANEL,
            foreground      = self.FG,
            fieldbackground = self.BG_PANEL,
            rowheight       = 22,
            font            = self.FONT,
            indent          = 12,
        )
        style.configure(
            "MsgTree.Treeview.Heading",
            background = self.BG_PANEL,
            foreground = self.FG_DIM,
            font       = self.FONT_SM,
        )
        style.map("MsgTree.Treeview", background=[("selected", "#45475a")])
        style.configure(
            "Inject.TCombobox",
            fieldbackground  = self.BG_ENTRY,
            background       = self.BG_ENTRY,
            foreground       = self.FG_ENTRY,
            selectbackground = "#b0b0c8",
            selectforeground = self.FG_ENTRY,
        )
        self.option_add("*TCombobox*Listbox.background",       self.BG_ENTRY)
        self.option_add("*TCombobox*Listbox.foreground",       self.FG_ENTRY)
        self.option_add("*TCombobox*Listbox.selectBackground", "#b0b0c8")
        self.option_add("*TCombobox*Listbox.selectForeground", self.FG_ENTRY)

        # Paned layout: tree | form
        pane = tk.PanedWindow(self, orient=tk.HORIZONTAL,
                              bg=self.BG, sashwidth=4)
        pane.pack(fill=tk.BOTH, expand=True)

        # ── Left: message tree ────────────────────────────────────────────────
        tree_frame = tk.Frame(pane, bg=self.BG_PANEL, width=310)
        pane.add(tree_frame, minsize=260)

        tk.Label(tree_frame, text="Messages",
                 bg=self.BG_PANEL, fg=self.FG_DIM,
                 font=self.FONT_SM).pack(anchor=tk.W, padx=6, pady=(6, 2))

        self._tree = ttk.Treeview(
            tree_frame, style="MsgTree.Treeview",
            selectmode="browse", show="tree",
        )
        self._tree.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        self._tree.bind("<<TreeviewSelect>>", self._on_tree_select)

        # ── Right: form area ──────────────────────────────────────────────────
        form_outer = tk.Frame(pane, bg=self.BG)
        pane.add(form_outer, minsize=280)

        self._form_title = tk.Label(
            form_outer,
            text=("← Select a message" if not self._load_error
                  else f"⚠ Failed to load messages/:\n{self._load_error}"),
            bg=self.BG,
            fg=(self.FG_DIM if not self._load_error else self.FG_RED),
            font=self.FONT_BOLD, anchor=tk.W, justify=tk.LEFT,
        )
        self._form_title.pack(fill=tk.X, padx=10, pady=(8, 4))

        self._msg_id_label = tk.Label(
            form_outer, text="",
            bg=self.BG, fg=self.FG_DIM, font=self.FONT_SM, anchor=tk.W,
        )
        self._msg_id_label.pack(fill=tk.X, padx=10, pady=(0, 6))

        # Scrollable form interior
        self._form_canvas = tk.Canvas(form_outer, bg=self.BG,
                                      highlightthickness=0)
        self._form_scroll = ttk.Scrollbar(form_outer, orient=tk.VERTICAL,
                                          command=self._form_canvas.yview)
        self._form_canvas.configure(yscrollcommand=self._form_scroll.set)
        self._form_scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self._form_canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self._form_inner = tk.Frame(self._form_canvas, bg=self.BG)
        self._form_win = self._form_canvas.create_window(
            (0, 0), window=self._form_inner, anchor=tk.NW)
        self._form_inner.bind("<Configure>", self._on_form_configure)
        self._form_canvas.bind("<Configure>", self._on_canvas_configure)

        # ── Module selector row (Source / Destination) — always visible ───────
        mod_labels = [lbl for lbl, _ in self._module_entries]

        sep = tk.Frame(form_outer, bg="#313244", height=1)
        sep.pack(fill=tk.X, padx=10, pady=(6, 0))

        mod_row = tk.Frame(form_outer, bg=self.BG)
        mod_row.pack(fill=tk.X, padx=10, pady=(6, 2))

        # Source module
        tk.Label(mod_row, text="Source module",
                 bg=self.BG, fg=self.FG_DIM, font=self.FONT_SM,
                 width=16, anchor=tk.W).grid(row=0, column=0, sticky=tk.W, pady=3)
        self._src_var = tk.StringVar(value=mod_labels[1] if len(mod_labels) > 1 else mod_labels[0])
        self._src_combo = ttk.Combobox(
            mod_row, textvariable=self._src_var,
            values=mod_labels, state="readonly",
            style="Inject.TCombobox", font=self.FONT, width=28,
        )
        self._src_combo.grid(row=0, column=1, sticky=tk.W, padx=(4, 0))

        # Destination module
        tk.Label(mod_row, text="Destination module",
                 bg=self.BG, fg=self.FG_DIM, font=self.FONT_SM,
                 width=16, anchor=tk.W).grid(row=1, column=0, sticky=tk.W, pady=3)
        self._dst_var = tk.StringVar(value=mod_labels[0])   # default = BROADCAST
        self._dst_combo = ttk.Combobox(
            mod_row, textvariable=self._dst_var,
            values=mod_labels, state="readonly",
            style="Inject.TCombobox", font=self.FONT, width=28,
        )
        self._dst_combo.grid(row=1, column=1, sticky=tk.W, padx=(4, 0))

        # ── Send button row ───────────────────────────────────────────────────
        btn_row = tk.Frame(form_outer, bg=self.BG)
        btn_row.pack(fill=tk.X, side=tk.BOTTOM, padx=10, pady=8)

        self._send_btn = tk.Button(
            btn_row, text="Send  →",
            bg="#313244", fg=self.FG,
            activebackground=self.FG_BLUE, activeforeground=self.BG,
            font=self.FONT_BOLD, relief=tk.FLAT, padx=12, pady=6,
            state=tk.DISABLED, command=self._on_send,
        )
        self._send_btn.pack(side=tk.RIGHT)

        self._status_label = tk.Label(
            btn_row, text="",
            bg=self.BG, fg=self.FG_GREEN, font=self.FONT_SM,
        )
        self._status_label.pack(side=tk.LEFT)

        self._populate_tree()

    # ── Tree ──────────────────────────────────────────────────────────────────

    def _populate_tree(self):
        for cat in self._cat_order:
            if cat not in self._messages:
                continue
            parent_id = self._tree.insert(
                "", tk.END, text=cat, open=True, tags=("category",))
            self._tree.tag_configure(
                "category", foreground=self.FG_MAUVE, font=self.FONT_BOLD)
            for msg_name in self._messages[cat]:
                self._tree.insert(
                    parent_id, tk.END, text=msg_name,
                    values=(cat, msg_name), tags=("message",))
            self._tree.tag_configure("message", foreground=self.FG)

    # ── Form helpers ──────────────────────────────────────────────────────────

    def _on_form_configure(self, _event):
        self._form_canvas.configure(
            scrollregion=self._form_canvas.bbox("all"))

    def _on_canvas_configure(self, event):
        self._form_canvas.itemconfig(self._form_win, width=event.width)

    def _on_tree_select(self, _event):
        sel = self._tree.selection()
        if not sel:
            return
        values = self._tree.item(sel[0], "values")
        if not values:
            return       # category node — ignore
        cat, msg_name = values[0], values[1]
        self._render_form(msg_name, self._messages[cat][msg_name])

    def _render_form(self, msg_name: str, desc: dict):
        for w in self._form_inner.winfo_children():
            w.destroy()
        self._fields_ui = []
        self._desc = desc

        self._form_title.config(text=msg_name, fg=self.FG)
        self._msg_id_label.config(
            text=f"msg_id = 0x{desc['msg_id']:04X}  ({desc['msg_id']})")

        fields = desc.get("fields", [])
        if not fields:
            tk.Label(self._form_inner,
                     text="(no payload fields)",
                     bg=self.BG, fg=self.FG_DIM,
                     font=self.FONT_SM).pack(padx=10, pady=8, anchor=tk.W)
        else:
            for field in fields:
                self._add_field_row(field)

        self._send_btn.config(state=tk.NORMAL)
        self._status_label.config(text="")

    def _add_field_row(self, field: dict):
        """Render one label + input-widget row inside the scrollable form."""
        row = tk.Frame(self._form_inner, bg=self.BG)
        row.pack(fill=tk.X, padx=10, pady=4)

        label_text = field.get("label", field["name"])
        tk.Label(row, text=label_text,
                 bg=self.BG, fg=self.FG_DIM, font=self.FONT_SM,
                 width=22, anchor=tk.W).pack(side=tk.LEFT)

        ftype = field.get("type", "string")
        var   = None

        # ── enum ──────────────────────────────────────────────────────────────
        if ftype == "enum":
            options = field.get("options", [])   # [(label, value), …]
            labels  = [str(lbl) for lbl, _ in options]
            var     = tk.StringVar(value=labels[0] if labels else "")
            ttk.Combobox(
                row,
                textvariable=var, values=labels,
                state="readonly", style="Inject.TCombobox",
                font=self.FONT, width=22,
            ).pack(side=tk.LEFT, padx=(4, 0))

        # ── integer types ─────────────────────────────────────────────────────
        elif ftype in _INT_TYPES:
            lo      = field.get("min", _INT_BOUNDS[ftype][0])
            hi      = field.get("max", _INT_BOUNDS[ftype][1])
            default = field.get("default", lo)
            var = tk.StringVar(value=str(default))
            vcmd = (self.register(
                        lambda s, a=lo, b=hi: self._validate_int(s, a, b)),
                    "%P")
            tk.Entry(
                row,
                textvariable=var,
                bg=self.BG_ENTRY, fg=self.FG_ENTRY,
                insertbackground=self.FG_ENTRY,
                font=self.FONT, relief=tk.FLAT, width=18,
                validate="key", validatecommand=vcmd,
            ).pack(side=tk.LEFT, padx=(4, 0), ipady=3)
            tk.Label(row, text=f"{lo} – {hi}",
                     bg=self.BG, fg=self.FG_DIM,
                     font=self.FONT_SM).pack(side=tk.LEFT, padx=4)

        # ── float / double ────────────────────────────────────────────────────
        elif ftype in _FLOAT_TYPES:
            default = field.get("default", 0.0)
            var = tk.StringVar(value=str(default))
            vcmd = (self.register(self._validate_float), "%P")
            tk.Entry(
                row,
                textvariable=var,
                bg=self.BG_ENTRY, fg=self.FG_ENTRY,
                insertbackground=self.FG_ENTRY,
                font=self.FONT, relief=tk.FLAT, width=18,
                validate="key", validatecommand=vcmd,
            ).pack(side=tk.LEFT, padx=(4, 0), ipady=3)
            tk.Label(row, text=ftype,
                     bg=self.BG, fg=self.FG_DIM,
                     font=self.FONT_SM).pack(side=tk.LEFT, padx=4)

        # ── string ────────────────────────────────────────────────────────────
        else:
            default = field.get("default", "")
            var = tk.StringVar(value=str(default))
            tk.Entry(
                row,
                textvariable=var,
                bg=self.BG_ENTRY, fg=self.FG_ENTRY,
                insertbackground=self.FG_ENTRY,
                font=self.FONT, relief=tk.FLAT, width=26,
            ).pack(side=tk.LEFT, padx=(4, 0), ipady=3)

        self._fields_ui.append((field, var))

    # ── Validation ────────────────────────────────────────────────────────────

    @staticmethod
    def _validate_int(s: str, lo: int, hi: int) -> bool:
        if s in ("", "-"):
            return True
        try:
            return lo <= int(s) <= hi
        except ValueError:
            return False

    @staticmethod
    def _validate_float(s: str) -> bool:
        if s in ("", "-", ".", "-."):
            return True
        try:
            float(s)
            return True
        except ValueError:
            return False

    # ── Send ──────────────────────────────────────────────────────────────────

    def _on_send(self):
        if self._desc is None:
            return

        payload = {}
        for field, var in self._fields_ui:
            fname = field["name"]
            ftype = field.get("type", "string")
            raw   = var.get()

            if ftype == "enum":
                options = field.get("options", [])
                matched = next((v for lbl, v in options if str(lbl) == raw), None)
                if matched is None:
                    return self._err(f"Bad value for '{fname}'")
                payload[fname] = matched

            elif ftype in _INT_TYPES:
                if raw in ("", "-"):
                    return self._err(f"'{fname}' is required")
                try:
                    payload[fname] = int(raw)
                except ValueError:
                    return self._err(f"Invalid integer for '{fname}'")

            elif ftype in _FLOAT_TYPES:
                if raw in ("", "-", ".", "-."):
                    return self._err(f"'{fname}' is required")
                try:
                    payload[fname] = float(raw)
                except ValueError:
                    return self._err(f"Invalid number for '{fname}'")

            else:  # string
                payload[fname] = raw

        # Resolve Source / Destination module IDs from the combo selections
        src_label = self._src_var.get()
        dst_label = self._dst_var.get()
        src_id = next((mid for lbl, mid in self._module_entries if lbl == src_label), 0)
        dst_id = next((mid for lbl, mid in self._module_entries if lbl == dst_label), 0)

        cmd = {
            "id":   "SIM_MSG_INJECT",
            "data": {
                "msg_id":        self._desc["msg_id"],
                "src_module_id": src_id,
                "dst_module_id": dst_id,
                "payload":       payload,
            },
        }

        if self._send_fn:
            self._send_fn(cmd)
            self._status_label.config(
                text=f"✓ sent  msg_id=0x{self._desc['msg_id']:04X}  src={src_id}  dst={dst_id}",
                fg=self.FG_GREEN)
        else:
            self._status_label.config(text="⚠ no connection", fg=self.FG_RED)

    def _err(self, msg: str):
        self._status_label.config(text=msg, fg=self.FG_RED)
