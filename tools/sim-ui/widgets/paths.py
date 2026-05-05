"""
paths.py — resolves sim_paths.json entries to absolute filesystem paths.

sim_paths.json lives one level up (tools/sim-ui/sim_paths.json) and holds
paths relative to the repository root.  Edit that file to relocate any
simulator directory without touching widget source code.
"""

import json
import os

_WIDGETS_DIR = os.path.dirname(os.path.abspath(__file__))          # tools/sim-ui/widgets/
_SIM_UI_DIR  = os.path.dirname(_WIDGETS_DIR)                       # tools/sim-ui/
_REPO_ROOT   = os.path.normpath(os.path.join(_SIM_UI_DIR, "..", ".."))  # repo root

_cfg_file = os.path.join(_SIM_UI_DIR, "sim_paths.json")


def _load() -> dict:
    with open(_cfg_file) as f:
        raw = json.load(f)
    # skip meta keys that start with _
    return {
        k: os.path.normpath(os.path.join(_REPO_ROOT, v))
        for k, v in raw.items()
        if not k.startswith("_")
    }


sim_paths: dict = _load()
