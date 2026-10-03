"""
Auth adapters, selected with FERP_AUTH_MODE.

  none       Everyone who can reach the server is "local". Default — the server
             listens on 127.0.0.1 and is published only to the tailnet.
  tailscale  Trust the Tailscale-User-Login header that `tailscale serve` adds
             for tailnet users; optionally restrict to FERP_AUTH_ALLOWED_USERS.
             Only safe when the server is reachable solely through tailscale serve.

A password / OIDC provider can be added later as another class with the same
authenticate() signature.
"""

from typing import Mapping, Optional


class NoAuth:
    mode = "none"

    def authenticate(self, headers: Mapping[str, str]) -> Optional[str]:
        return "local"


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
