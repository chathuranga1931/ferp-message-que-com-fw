"""
Auth adapters, selected with FERP_AUTH_MODE.

  auto       Default.  Login with a user name and password (session cookie) as soon as
             at least one user account exists; before that, like "none".
  none       Everyone who can reach the server is "local" (full access).
  tailscale  Trust the Tailscale-User-Login header that `tailscale serve` adds
             for tailnet users; optionally restrict to FERP_AUTH_ALLOWED_USERS.
             Only safe when the server is reachable solely through tailscale serve.

Each provider answers authenticate(headers) -> user or None, and allows(user, permission) /
is_admin(user) for the per-route permission checks.
"""

from http.cookies import SimpleCookie
from typing import Mapping, Optional

SESSION_COOKIE = "ferp_session"


class NoAuth:
    mode = "none"

    def authenticate(self, headers: Mapping[str, str]) -> Optional[str]:
        return "local"

    def allows(self, user: str, permission: str) -> bool:
        return True

    def is_admin(self, user: str) -> bool:
        return True


def session_token(headers: Mapping[str, str]) -> str:
    raw = headers.get("cookie") or ""
    try:
        c = SimpleCookie(raw)
    except Exception:
        return ""
    m = c.get(SESSION_COOKIE)
    return m.value if m else ""


class SessionAuth:
    """User accounts with passwords (services.users.Users); open like NoAuth until the first user exists."""
    mode = "password"

    def __init__(self, users):
        self.users = users

    def _open(self) -> bool:
        return not self.users.any()

    def authenticate(self, headers: Mapping[str, str]) -> Optional[str]:
        if self._open():
            return "local"
        return self.users.user_for(session_token(headers))

    def allows(self, user: str, permission: str) -> bool:
        return self._open() or self.users.allows(user, permission)

    def is_admin(self, user: str) -> bool:
        return self._open() or self.users.is_admin(user)


class TailscaleHeaderAuth:
    mode = "tailscale"

    def __init__(self, allowed_users: set[str]):
        self._allowed = {u.lower() for u in allowed_users}

    def authenticate(self, headers: Mapping[str, str]) -> Optional[str]:
        user = (headers.get("tailscale-user-login") or "").strip().lower()
        if not user:
            return None
        if self._allowed and user not in self._allowed:
            return None
        return user

    def allows(self, user: str, permission: str) -> bool:
        return True

    def is_admin(self, user: str) -> bool:
        return True
