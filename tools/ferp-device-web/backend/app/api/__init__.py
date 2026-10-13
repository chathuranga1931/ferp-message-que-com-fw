"""REST API (/api/...) split by area, plus the live event WebSocket."""

from fastapi import APIRouter

from . import assets, auth, bench, devices, fleet, history, logs, ota, releases, system
from .ws import events_ws

public = APIRouter()                      # no login needed: health, login, logout
public.include_router(system.public)
public.include_router(auth.public)
routers = [system.router, auth.router, devices.router, assets.router, ota.router, fleet.router, history.router, logs.router, releases.router, bench.router]

__all__ = ["events_ws", "public", "routers"]
