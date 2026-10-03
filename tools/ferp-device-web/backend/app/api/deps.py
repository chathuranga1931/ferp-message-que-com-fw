"""Shared API dependencies and helpers."""

from fastapi import APIRouter, Depends, HTTPException, Request

from ..container import Container
from ..models import Device
from ..services.mqtt_hub import NotConnectedError


def container(request: Request) -> Container:
    return request.app.state.container


def current_user(request: Request) -> str:
    user = container(request).auth.authenticate(request.headers)
    if user is None:
        raise HTTPException(401, "Not authorised")
    return user


def protected() -> APIRouter:
    return APIRouter(prefix="/api", dependencies=[Depends(current_user)])


def get_device(c: Container, device_id: str) -> Device:
    dev = c.devices.get(device_id)
    if dev is None:
        raise HTTPException(404, f"Unknown device {device_id}")
    return dev


def get_devices(c: Container, device_ids: list[str] | None) -> list[Device]:
    if device_ids is None:
        return c.devices.list()
    return [get_device(c, i) for i in device_ids]


def guard(fn, *args, **kwargs):
    """Map service exceptions to HTTP errors."""
    try:
        return fn(*args, **kwargs)
    except NotConnectedError as exc:
        raise HTTPException(409, str(exc))
    except KeyError as exc:
        raise HTTPException(404, str(exc).strip("'\""))
    except ValueError as exc:
        raise HTTPException(400, str(exc))
    except RuntimeError as exc:
        raise HTTPException(409, str(exc))
