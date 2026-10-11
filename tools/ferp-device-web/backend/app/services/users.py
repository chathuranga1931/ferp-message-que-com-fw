"""
User accounts, permissions and login sessions.

    users      {username: {username, pw, admin, permissions, disabled, created, last_login}}
    sessions   {sha256(token): {user, created, expires}}

Passwords are stored as scrypt hashes ("scrypt$<salt hex>$<hash hex>"), never in plain text.
A session token lives only in the browser's HttpOnly cookie; the server keeps its SHA-256.
An admin has every permission and manages the accounts.
"""

import hashlib
import hmac
import secrets
import threading
import time
from typing import Optional

from ..ports import DocumentStore

USERS, SESSIONS = "users", "sessions"
SESSION_DAYS = 7
_RENEW_S = 3600                    # extend a session at most once an hour (keeps writes rare)

# (key, label, what it allows) — viewing Fleet, Sheds, Cables, firmware list and OTA progress needs none
PERMISSIONS: list[tuple[str, str, str]] = [
    ("devices",   "Manage devices",       "Add, edit and remove devices, sheds, cables and device history"),
    ("workspace", "Workspace",            "Send messages to devices, read device info, favourites"),
    ("config",    "Device configuration", "Read and write config keys, config snapshots"),
    ("ota",       "Run OTA",              "Start and abort firmware updates (single and batch)"),
    ("firmware",  "Firmware library",     "Upload, import, archive and delete firmware bundles"),
    ("logs",      "Debug logs",           "Cloud logs of the devices"),
    ("history",   "History",              "Console and audit history"),
    ("settings",  "Settings",             "Server settings, MQTT connection, restart the server"),
]
PERMISSION_KEYS = {k for k, _, _ in PERMISSIONS}


def _hash(password: str, salt: Optional[bytes] = None) -> str:
    salt = salt or secrets.token_bytes(16)
    h = hashlib.scrypt(password.encode(), salt=salt, n=2 ** 14, r=8, p=1, dklen=32)
    return f"scrypt${salt.hex()}${h.hex()}"


def _check(password: str, stored: str) -> bool:
    try:
        _, salt, _h = stored.split("$")
        return hmac.compare_digest(_hash(password, bytes.fromhex(salt)), stored)
    except ValueError:
        return False


def _token_id(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


def public_view(u: dict) -> dict:
    return {k: u.get(k) for k in ("username", "admin", "permissions", "disabled", "created", "last_login")}


class Users:
    def __init__(self, docs: DocumentStore):
        self._docs = docs
        self._lock = threading.Lock()

    # ── accounts ──────────────────────────────────────────────────────────────

    def any(self) -> bool:
        return bool(self._docs.list(USERS))

    def list(self) -> list[dict]:
        return sorted((public_view(u) for u in self._docs.list(USERS)), key=lambda u: u["username"])

    def get(self, username: str) -> Optional[dict]:
        return self._docs.get(USERS, (username or "").strip().lower())

    def create(self, username: str, password: str, admin: bool = False, permissions: Optional[list[str]] = None) -> dict:
        name = (username or "").strip().lower()
        if not name or not name.replace(".", "").replace("-", "").replace("_", "").isalnum():
            raise ValueError("Username: letters, digits, '.', '-' or '_' only")
        self._check_password(password)
        with self._lock:
            if self._docs.get(USERS, name):
                raise ValueError(f"User '{name}' already exists")
            u = {"username": name, "pw": _hash(password), "admin": bool(admin),
                 "permissions": self._clean(permissions), "disabled": False, "created": time.time(), "last_login": None}
            self._docs.put(USERS, name, u)
        return public_view(u)

    def update(self, username: str, *, acting: str, admin: Optional[bool] = None, permissions: Optional[list[str]] = None,
               disabled: Optional[bool] = None, password: Optional[str] = None) -> dict:
        with self._lock:
            u = self.get(username)
            if u is None:
                raise KeyError(f"Unknown user {username}")
            if admin is not None or disabled is not None:
                self._guard_last_admin(u, acting, losing=(admin is False) or (disabled is True))
            if admin is not None:
                u["admin"] = bool(admin)
            if permissions is not None:
                u["permissions"] = self._clean(permissions)
            if disabled is not None:
                u["disabled"] = bool(disabled)
            if password:
                self._check_password(password)
                u["pw"] = _hash(password)
            self._docs.put(USERS, u["username"], u)
            if disabled or password:
                self._drop_sessions(u["username"])     # disabled / new password → log out everywhere
        return public_view(u)

    def delete(self, username: str, acting: str) -> bool:
        with self._lock:
            u = self.get(username)
            if u is None:
                return False
            self._guard_last_admin(u, acting, losing=True)
            self._drop_sessions(u["username"])
            return self._docs.delete(USERS, u["username"])

    def allows(self, username: str, permission: str) -> bool:
        u = self.get(username)
        return bool(u and not u.get("disabled") and (u.get("admin") or permission in (u.get("permissions") or [])))

    def is_admin(self, username: str) -> bool:
        u = self.get(username)
        return bool(u and not u.get("disabled") and u.get("admin"))

    # ── sessions ──────────────────────────────────────────────────────────────

    def login(self, username: str, password: str) -> Optional[str]:
        """Returns a new session token, or None (unknown user, wrong password or disabled)."""
        u = self.get(username)
        if u is None or u.get("disabled") or not _check(password, u.get("pw", "")):
            time.sleep(0.5)                            # slow down guessing
            return None
        token = secrets.token_urlsafe(32)
        now = time.time()
        sid = _token_id(token)
        self._docs.put(SESSIONS, sid, {"sid": sid, "user": u["username"], "created": now, "renewed": now,
                                       "expires": now + SESSION_DAYS * 86400})
        u["last_login"] = now
        self._docs.put(USERS, u["username"], u)
        self._prune()
        return token

    def logout(self, token: str) -> None:
        if token:
            self._docs.delete(SESSIONS, _token_id(token))

    def user_for(self, token: str) -> Optional[str]:
        if not token:
            return None
        sid = _token_id(token)
        s = self._docs.get(SESSIONS, sid)
        now = time.time()
        if s is None or s.get("expires", 0) < now:
            return None
        u = self.get(s["user"])
        if u is None or u.get("disabled"):
            return None
        if now - s.get("renewed", 0) > _RENEW_S:      # sliding expiry
            s.update(renewed=now, expires=now + SESSION_DAYS * 86400)
            self._docs.put(SESSIONS, sid, s)
        return u["username"]

    # ── helpers ───────────────────────────────────────────────────────────────

    @staticmethod
    def _clean(perms: Optional[list[str]]) -> list[str]:
        return sorted({p for p in (perms or []) if p in PERMISSION_KEYS})

    @staticmethod
    def _check_password(pw: str) -> None:
        if not pw or len(pw) < 8:
            raise ValueError("Password: at least 8 characters")

    def _guard_last_admin(self, u: dict, acting: str, losing: bool) -> None:
        if not (losing and u.get("admin")):
            return
        if u["username"] == acting:
            raise ValueError("You cannot remove your own admin access or disable / delete your own account")
        admins = [x for x in self._docs.list(USERS) if x.get("admin") and not x.get("disabled")]
        if len(admins) <= 1:
            raise ValueError("Keep at least one active admin")

    def _drop_sessions(self, username: str) -> None:
        for sid, s in list(self._iter_sessions()):
            if s.get("user") == username:
                self._docs.delete(SESSIONS, sid)

    def _iter_sessions(self):
        for s in self._docs.list(SESSIONS):
            sid = s.get("sid")
            if sid:
                yield sid, s

    def _prune(self) -> None:
        now = time.time()
        for sid, s in list(self._iter_sessions()):
            if s.get("expires", 0) < now:
                self._docs.delete(SESSIONS, sid)
