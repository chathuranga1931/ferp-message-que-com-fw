"""Device registry, per-device operations, snapshots and favourites."""

from fastapi import Depends, HTTPException

from ..container import Container
from ..models import (ConfigWriteIn, DeviceIn, FavoriteIn, KeysIn, RequestMessageIn, SendMessageIn,
                      SnapshotApplyIn, SnapshotIn)
from ..services.mqtt_hub import ResponseTimeout
from .deps import container, current_user, get_device, get_devices, guard, protected

router = protected()


# ── registry ────────────────────────────────────────────────────────────────

@router.get("/devices")
def list_devices(c: Container = Depends(container)):
    return c.devices.list()


@router.post("/devices")
def add_device(dev: DeviceIn, c: Container = Depends(container), user: str = Depends(current_user)):
    new = c.devices.add(dev)
    c.console.log("info", f"Device added: {new.label}")
    c.audit.record(user, "device.add", new, mac=new.mac, uuid=new.uuid, group=new.group)
    return new


@router.put("/devices/{device_id}")
def update_device(device_id: str, dev: DeviceIn, c: Container = Depends(container), user: str = Depends(current_user)):
    old = get_device(c, device_id)
    upd = c.devices.update(device_id, dev)
    changes = {k: [getattr(old, k), getattr(upd, k)] for k in DeviceIn.model_fields if getattr(old, k) != getattr(upd, k)}
    c.audit.record(user, "device.update", upd, changes=changes)
    return upd


@router.delete("/devices/{device_id}")
def delete_device(device_id: str, c: Container = Depends(container), user: str = Depends(current_user)):
    dev = get_device(c, device_id)
    c.devices.delete(device_id)
    c.audit.record(user, "device.delete", dev)
    return {"ok": True}


# ── operations ──────────────────────────────────────────────────────────────

@router.get("/devices/{device_id}/state")
def device_state(device_id: str, c: Container = Depends(container)):
    dev = get_device(c, device_id)
    return {**c.ops.state(dev), "ota": c.ota.session(device_id),
            "topic_base": guard(c.ops.base, dev) if dev.mqtt_id else None}


@router.post("/devices/{device_id}/send")
def send_message(device_id: str, body: SendMessageIn, c: Container = Depends(container),
                 user: str = Depends(current_user)):
    if body.msg not in c.catalog.messages:
        raise HTTPException(400, f"Unknown message {body.msg}")
    dev = get_device(c, device_id)
    seq = guard(c.ops.send, dev, body.msg, body.data)
    c.audit.record(user, "message.send", dev, msg=body.msg, data=body.data)
    return {"seq": seq}


@router.post("/devices/{device_id}/request")
def request_message(device_id: str, body: RequestMessageIn, c: Container = Depends(container),
                    user: str = Depends(current_user)):
    """Send a command and wait for its reply (blocking, runs in the worker pool)."""
    for name in (body.msg, body.expect):
        if name not in c.catalog.messages:
            raise HTTPException(400, f"Unknown message {name}")
    dev = get_device(c, device_id)
    try:
        resp = guard(c.ops.request, dev, body.msg, body.data, body.expect, body.timeout)
    except ResponseTimeout as exc:
        raise HTTPException(504, str(exc))
    c.audit.record(user, "message.request", dev, msg=body.msg, data=body.data)
    return {"msg": resp.get("msg"), "seq": resp.get("seq"), "data": resp.get("data") or {}}


@router.post("/devices/{device_id}/site-info/read")
async def read_site_info(device_id: str, c: Container = Depends(container)):
    """Pump IDs (NOZZLE_0/1_ID) and board version (HW_VERSION) as reported by the device — not saved."""
    from fastapi.concurrency import run_in_threadpool
    dev = get_device(c, device_id)
    if not c.hub.connected:
        raise HTTPException(409, "MQTT is not connected")
    return await run_in_threadpool(c.ops.read_site_info, dev)


@router.post("/devices/{device_id}/config/read")
def config_read(device_id: str, body: KeysIn, c: Container = Depends(container)):
    return {"job_id": guard(c.ops.start_read_config, get_device(c, device_id), body.keys)}


@router.post("/devices/{device_id}/config/write")
def config_write(device_id: str, body: ConfigWriteIn, c: Container = Depends(container),
                 user: str = Depends(current_user)):
    if not body.values:
        raise HTTPException(400, "Nothing to write")
    values = [(v.key, v.value) for v in body.values]
    return {"job_id": guard(c.ops.start_write_config, get_device(c, device_id), values, user=user)}


@router.post("/devices/{device_id}/devinfo/read")
def devinfo_read(device_id: str, body: KeysIn, c: Container = Depends(container)):
    return {"job_id": guard(c.ops.start_read_devinfo, get_device(c, device_id), body.keys)}


@router.post("/jobs/{job_id}/cancel")
def cancel_job(job_id: str, c: Container = Depends(container)):
    return {"ok": c.ops.cancel_job(job_id)}


# ── snapshots ───────────────────────────────────────────────────────────────

@router.get("/snapshots")
def list_snapshots(c: Container = Depends(container)):
    return c.snapshots.list()


@router.get("/snapshots/{sid}")
def get_snapshot(sid: str, c: Container = Depends(container)):
    snap = c.snapshots.get(sid)
    if snap is None:
        raise HTTPException(404, f"Unknown snapshot {sid}")
    return snap


@router.post("/snapshots")
def create_snapshot(body: SnapshotIn, c: Container = Depends(container), user: str = Depends(current_user)):
    dev = get_device(c, body.device_id) if body.device_id else None
    return guard(c.snapshots.create, user, body.name, body.notes, dev, body.values)


@router.delete("/snapshots/{sid}")
def delete_snapshot(sid: str, c: Container = Depends(container), user: str = Depends(current_user)):
    if not c.snapshots.delete(user, sid):
        raise HTTPException(404, f"Unknown snapshot {sid}")
    return {"ok": True}


@router.post("/snapshots/{sid}/apply")
def apply_snapshot(sid: str, body: SnapshotApplyIn, c: Container = Depends(container),
                   user: str = Depends(current_user)):
    devices = get_devices(c, body.device_ids)
    results = guard(c.snapshots.apply, user, sid, devices, body.keys, body.only_diff)
    snap = c.snapshots.get(sid)
    if snap:   # history entry + "loaded snapshot" on each device page
        c.assets.record_snapshot_applied(user, devices, snap, results)
    return {"results": results}


# ── favourites ──────────────────────────────────────────────────────────────

@router.get("/favorites")
def list_favorites(c: Container = Depends(container)):
    return c.favorites.list()


@router.post("/favorites")
def add_favorite(body: FavoriteIn, c: Container = Depends(container), user: str = Depends(current_user)):
    if body.msg not in c.catalog.messages:
        raise HTTPException(400, f"Unknown message {body.msg}")
    return c.favorites.add(user, body.name, body.msg, body.data)


@router.delete("/favorites/{fid}")
def delete_favorite(fid: str, c: Container = Depends(container), user: str = Depends(current_user)):
    if not c.favorites.delete(user, fid):
        raise HTTPException(404, f"Unknown favourite {fid}")
    return {"ok": True}


@router.get("/messages/recent")
def recent_messages(limit: int = 20, c: Container = Depends(container)):
    return c.favorites.recent(limit)
