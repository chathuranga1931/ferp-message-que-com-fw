"""REST API (/api/...) split by area, plus the live event WebSocket."""

from . import devices, fleet, history, logs, ota, system
from .ws import events_ws

public = system.public
routers = [system.router, devices.router, ota.router, fleet.router, history.router, logs.router]

__all__ = ["events_ws", "public", "routers"]
