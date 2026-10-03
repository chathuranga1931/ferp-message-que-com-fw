"""FastAPI application: API + WebSocket + the built React UI on one port."""

import asyncio
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles

from .api import events_ws, public, routers
from .container import Container
from .server_settings import ServerSettings

log = logging.getLogger("ferp.web")


def create_app(settings: ServerSettings | None = None) -> FastAPI:
    settings = settings or ServerSettings()
    c = Container(settings)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        bus_bind = getattr(c.bus, "bind_loop", None)
        if bus_bind:
            bus_bind(asyncio.get_running_loop())
        c.console.log("info", f"FERP Device Web started (auth={c.auth.mode}, storage={settings.storage})")
        if c.config.mqtt.auto_connect:
            c.hub.connect()
        yield
        c.shutdown()

    app = FastAPI(title="FERP Device Web", lifespan=lifespan)
    app.state.container = c
    app.include_router(public)
    for r in routers:
        app.include_router(r)
    app.add_api_websocket_route("/ws", events_ws)

    # Unknown /api/* must not fall through to the static UI mount (which answers 405 to POSTs).
    @app.api_route("/api/{path:path}", methods=["GET", "POST", "PUT", "PATCH", "DELETE"], include_in_schema=False)
    def unknown_api(path: str):
        raise HTTPException(404, f"Unknown API endpoint /api/{path} — if the app was just updated, restart the server")

    if (settings.frontend_dist / "index.html").exists():
        app.mount("/", StaticFiles(directory=settings.frontend_dist, html=True), name="ui")
    else:
        @app.get("/", response_class=HTMLResponse)
        def no_ui():
            return ("<h3>FERP Device Web — API is running</h3>"
                    "<p>The UI is not built yet: run <code>npm run build</code> in "
                    "<code>tools/ferp-device-web/frontend</code>. API docs: <a href='/docs'>/docs</a></p>")
    return app
