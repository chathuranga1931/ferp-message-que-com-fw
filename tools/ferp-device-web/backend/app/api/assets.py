"""Sheds (customer sites), wire harness catalogue and per-device history."""

from fastapi import Depends, HTTPException

from ..container import Container
from ..models import DeviceLogIn, HarnessIn, ShedIn
from .deps import container, current_user, get_device, guard, protected

router = protected()


# ── sheds ───────────────────────────────────────────────────────────────────

@router.get("/sheds")
def list_sheds(c: Container = Depends(container)):
    return c.assets.sheds()


@router.post("/sheds")
def add_shed(body: ShedIn, c: Container = Depends(container), user: str = Depends(current_user)):
    return guard(c.assets.save_shed, user, body)


@router.put("/sheds/{sid}")
def update_shed(sid: str, body: ShedIn, c: Container = Depends(container), user: str = Depends(current_user)):
    return guard(c.assets.save_shed, user, body, sid)


@router.delete("/sheds/{sid}")
def delete_shed(sid: str, c: Container = Depends(container), user: str = Depends(current_user)):
    if not c.assets.delete_shed(user, sid):
        raise HTTPException(404, "Unknown shed")
    return {"ok": True}


# ── harness catalogue ───────────────────────────────────────────────────────

@router.get("/harnesses")
def list_harnesses(c: Container = Depends(container)):
    return c.assets.harnesses()


@router.put("/harnesses/{hid}")
def save_harness(hid: str, body: HarnessIn, c: Container = Depends(container), user: str = Depends(current_user)):
    if body.id != hid:
        raise HTTPException(400, "Harness id in the body does not match the URL")
    return c.assets.save_harness(user, body)


@router.delete("/harnesses/{hid}")
def delete_harness(hid: str, c: Container = Depends(container), user: str = Depends(current_user)):
    if not guard(c.assets.delete_harness, user, hid):
        raise HTTPException(404, "Unknown harness")
    return {"ok": True}


# ── device history ──────────────────────────────────────────────────────────

@router.get("/devices/{device_id}/log")
def device_log(device_id: str, c: Container = Depends(container)):
    get_device(c, device_id)
    return c.assets.log(device_id)


@router.post("/devices/{device_id}/log")
def add_device_log(device_id: str, body: DeviceLogIn, c: Container = Depends(container),
                   user: str = Depends(current_user)):
    return c.assets.add_log(user, get_device(c, device_id), body)


@router.put("/device-log/{eid}")
def update_device_log(eid: str, body: DeviceLogIn, c: Container = Depends(container),
                      user: str = Depends(current_user)):
    return guard(c.assets.update_log, user, eid, body)


@router.delete("/device-log/{eid}")
def delete_device_log(eid: str, c: Container = Depends(container), user: str = Depends(current_user)):
    if not c.assets.delete_log(user, eid):
        raise HTTPException(404, "Unknown log entry")
    return {"ok": True}


@router.get("/device-log/open-issues")
def open_issues(c: Container = Depends(container)):
    """Open issues across all devices: {device_id: count} (for badges in lists)."""
    out: dict[str, int] = {}
    for e in c.assets.all_log():
        if e.get("kind") == "issue" and e.get("status") == "open":
            out[e["device_id"]] = out.get(e["device_id"], 0) + 1
    return out
