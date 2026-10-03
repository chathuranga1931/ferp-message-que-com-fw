"""
Server (deployment) settings — environment variables, optionally from backend/.env.
These describe *where/how* the app runs; the app's own behaviour (MQTT broker,
timeouts, ...) is the AppConfig JSON edited from the Settings page.

  FERP_WEB_HOST            127.0.0.1      bind address (the Windows service sets 0.0.0.0)
  FERP_WEB_PORT            8700
  FERP_DATA_DIR            backend/data   app-config.json, devices.json, blobs/
  FERP_STORAGE             local          adapter set (local | future: gcp, aws)
  FERP_AUTH_MODE           none           none | tailscale
  FERP_AUTH_ALLOWED_USERS                 comma-separated tailnet logins (tailscale mode)
  FERP_MESSAGES_DIR                       override message definitions directory
  FERP_FRONTEND_DIST       frontend/dist  built web UI
  FERP_LOG_LEVEL           info
"""

import os
from dataclasses import dataclass, field
from pathlib import Path

BACKEND_DIR = Path(__file__).resolve().parent.parent
WEB_DIR     = BACKEND_DIR.parent
REPO_DIR    = WEB_DIR.parent.parent
REPO_MSGS   = REPO_DIR / "src" / "app-messages" / "messages"
REPO_RELEASES = REPO_DIR / "releases"


def _env(name: str, default: str) -> str:
    return os.environ.get(name, "").strip() or default


@dataclass
class ServerSettings:
    host:          str  = field(default_factory=lambda: _env("FERP_WEB_HOST", "127.0.0.1"))
    port:          int  = field(default_factory=lambda: int(_env("FERP_WEB_PORT", "8700")))
    data_dir:      Path = field(default_factory=lambda: Path(_env("FERP_DATA_DIR", str(BACKEND_DIR / "data"))))
    storage:       str  = field(default_factory=lambda: _env("FERP_STORAGE", "local"))
    auth_mode:     str  = field(default_factory=lambda: _env("FERP_AUTH_MODE", "none"))
    allowed_users: set  = field(default_factory=lambda: {u.strip() for u in _env("FERP_AUTH_ALLOWED_USERS", "").split(",") if u.strip()})
    frontend_dist: Path = field(default_factory=lambda: Path(_env("FERP_FRONTEND_DIST", str(WEB_DIR / "frontend" / "dist"))))
    log_level:     str  = field(default_factory=lambda: _env("FERP_LOG_LEVEL", "info"))
    messages_dirs: list = field(default_factory=lambda: [Path(p) for p in (
        os.environ.get("FERP_MESSAGES_DIR", ""),     # explicit override
        str(REPO_MSGS),                              # live definitions when inside the firmware repo
        str(BACKEND_DIR / "messages-json"),          # synced copy (standalone repo)
    ) if p])
    config_defaults: Path = BACKEND_DIR / "config" / "app-config.default.json"
    devices_seed:    Path = BACKEND_DIR / "config" / "devices.seed.json"
