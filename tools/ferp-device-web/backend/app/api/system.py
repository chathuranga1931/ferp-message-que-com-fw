"""Health, identity, app settings, MQTT connection, message catalog, live console, restart."""

import os
import threading

from fastapi import APIRouter, Depends, Request

from ..container import Container
from ..models import PASSWORD_MASK, AppConfig
from ..version import API_VERSION
from .deps import container, current_user, protected

public = APIRouter(prefix="/api")
router = protected()


@public.get("/health")
def health(request: Request):
    return {"ok": True, "api_version": API_VERSION, "started": container(request).started}


@router.post("/system/restart")
def restart(c: Container = Depends(container), user: str = Depends(current_user)):
    """Exit the process; the Windows service (WinSW onfailure=restart) starts it again with the
    code currently on disk. Use after pulling new code. Takes ~10 s."""
    c.audit.record(user, "system.restart")
    c.console.log("warn", f"Server restart requested by {user}")

    def _exit():
        try:
            c.shutdown()
        finally:
            os._exit(3)          # non-zero → service manager restarts us
    threading.Timer(0.5, _exit).start()
    return {"ok": True}


@router.get("/me")
def me(request: Request, user: str = Depends(current_user)):
    return {"user": user, "auth_mode": container(request).auth.mode}


@router.get("/config")
def get_config(c: Container = Depends(container)):
    return c.config.masked()


@router.put("/config")
def put_config(cfg: AppConfig, c: Container = Depends(container), user: str = Depends(current_user)):
    old = c.config
    if cfg.mqtt.password == PASSWORD_MASK:
        cfg.mqtt.password = old.mqtt.password
    if cfg.logs.key_passphrase == PASSWORD_MASK:
        cfg.logs.key_passphrase = old.logs.key_passphrase
    mqtt_changed = cfg.mqtt.model_dump(exclude={"brokers", "auto_connect"}) != \
                   old.mqtt.model_dump(exclude={"brokers", "auto_connect"})
    c.save_config(cfg)
    changed = sorted(k for k in cfg.model_dump() if getattr(cfg, k) != getattr(old, k))
    c.audit.record(user, "settings.save", sections=changed)
    reconnected = False
    if mqtt_changed and c.hub.status()["state"] != "disconnected":
        c.hub.connect()
        reconnected = True
    c.console.log("info", "Settings saved" + (" — MQTT reconnecting with new settings" if reconnected else ""))
    return {"config": c.config.masked(), "reconnected": reconnected}


@router.get("/mqtt")
def mqtt_status(c: Container = Depends(container)):
    return c.hub.status()


@router.post("/mqtt/connect")
def mqtt_connect(c: Container = Depends(container), user: str = Depends(current_user)):
    c.hub.connect()
    c.audit.record(user, "mqtt.connect", host=c.config.mqtt.host, port=c.config.mqtt.port)
    return c.hub.status()


@router.post("/mqtt/disconnect")
def mqtt_disconnect(c: Container = Depends(container), user: str = Depends(current_user)):
    c.hub.disconnect()
    c.audit.record(user, "mqtt.disconnect")
    return c.hub.status()


@router.get("/catalog")
def catalog(c: Container = Depends(container)):
    return c.catalog.to_json()


@router.post("/catalog/reload")
def catalog_reload(c: Container = Depends(container)):
    c.catalog.reload()
    return c.catalog.to_json()


@router.get("/console")
def console_tail(limit: int = 500, c: Container = Depends(container)):
    return c.console.tail(limit)


@router.delete("/console")
def console_clear(c: Container = Depends(container)):
    c.console.clear()
    return {"ok": True}
