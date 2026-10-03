"""Fleet presence and probing."""

from fastapi import Depends, HTTPException

from ..container import Container
from ..models import ProbeIn
from ..services.mqtt_hub import NotConnectedError
from .deps import container, get_devices, protected

router = protected()


@router.get("/fleet")
def fleet(c: Container = Depends(container)):
    return c.fleet.snapshot()


@router.post("/fleet/probe")
def probe(body: ProbeIn, c: Container = Depends(container)):
    if not c.hub.connected:
        raise HTTPException(409, str(NotConnectedError("MQTT is not connected")))
    return {"probing": c.fleet.probe(get_devices(c, body.device_ids))}


@router.delete("/fleet/{topic_id}")
def forget(topic_id: str, c: Container = Depends(container)):
    return {"ok": c.fleet.forget(topic_id)}
