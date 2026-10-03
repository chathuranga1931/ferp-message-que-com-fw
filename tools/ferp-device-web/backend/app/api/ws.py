"""Live event WebSocket (/ws)."""

import asyncio

from fastapi import WebSocket, WebSocketDisconnect

from ..container import Container
from ..version import API_VERSION


async def events_ws(ws: WebSocket):
    c: Container = ws.app.state.container
    if c.auth.authenticate(ws.headers) is None:
        await ws.close(code=4401)
        return
    await ws.accept()
    q = c.bus.subscribe()
    try:
        await ws.send_json({"type": "hello", "api_version": API_VERSION, "mqtt": c.hub.status(), "console": c.console.tail(500),
                            "ota": c.ota.sessions(), "batches": c.batch_ota.list()})

        async def drain_incoming():      # detect client going away; ignore pings
            while True:
                await ws.receive_text()

        reader = asyncio.create_task(drain_incoming())
        try:
            while not reader.done():
                try:
                    event = await asyncio.wait_for(q.get(), timeout=20)
                except asyncio.TimeoutError:
                    event = {"type": "ping"}
                await ws.send_json(event)
        finally:
            reader.cancel()
    except (WebSocketDisconnect, RuntimeError):
        pass
    finally:
        c.bus.unsubscribe(q)
