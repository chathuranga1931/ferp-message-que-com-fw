"""Firmware library, single-device OTA and batch OTA."""

from fastapi import Depends, File, Form, HTTPException, UploadFile
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import Response

from ..container import Container
from ..models import BatchOtaIn, FirmwareImportIn, FirmwarePatchIn, OtaStartIn
from .deps import container, current_user, get_device, get_devices, guard, protected

router = protected()


@router.get("/firmware")
def list_firmware(c: Container = Depends(container)):
    return c.firmware.list()


@router.post("/firmware")
async def upload_firmware(file: UploadFile = File(...), notes: str = Form(""), c: Container = Depends(container),
                          user: str = Depends(current_user)):
    """Upload a .bdl bundle (multipart field "file", optional "notes").

    Scripted:  curl -F file=@ferp_esp32_main_v1.0.0.140.bdl -F notes="nightly" http://<host>:8700/api/firmware
    """
    data = await file.read()
    try:
        meta = await run_in_threadpool(c.firmware.add, file.filename or "bundle.bdl", data, user, "upload", notes)
    except ValueError as exc:
        raise HTTPException(400, f"Not a valid .bdl bundle: {exc}")
    if not meta.get("duplicate"):
        c.audit.record(user, "firmware.upload", filename=meta["filename"], target=meta["name"], version=meta["version"])
    return meta


@router.get("/firmware/scan")
def scan_firmware(c: Container = Depends(container)):
    """.bdl files in the bundle folders (Settings → OTA; default: the repo's releases/)."""
    return c.firmware.scan()


@router.post("/firmware/import")
def import_firmware(body: FirmwareImportIn, c: Container = Depends(container), user: str = Depends(current_user)):
    results = c.firmware.import_paths(body.paths, user)
    for r in results:
        if "id" in r and not r.get("duplicate"):
            c.audit.record(user, "firmware.upload", filename=r["filename"], target=r["name"],
                           version=r["version"], source="folder")
    return {"results": results}


@router.get("/firmware/{firmware_id}/download")
def download_firmware(firmware_id: str, c: Container = Depends(container)):
    meta, data = guard(c.firmware.raw, firmware_id)
    return Response(data, media_type="application/octet-stream",
                    headers={"Content-Disposition": f'attachment; filename="{meta["filename"]}"'})


@router.patch("/firmware/{firmware_id}")
def patch_firmware(firmware_id: str, body: FirmwarePatchIn, c: Container = Depends(container)):
    return guard(c.firmware.set_notes, firmware_id, body.notes)


@router.delete("/firmware/{firmware_id}")
def delete_firmware(firmware_id: str, c: Container = Depends(container), user: str = Depends(current_user)):
    meta = c.firmware.meta(firmware_id) or {}
    c.firmware.delete(firmware_id)
    c.audit.record(user, "firmware.delete", id=firmware_id, filename=meta.get("filename"), version=meta.get("version"))
    return {"ok": True}


@router.get("/ota")
def ota_sessions(c: Container = Depends(container)):
    return c.ota.sessions()


@router.post("/devices/{device_id}/ota")
def ota_start(device_id: str, body: OtaStartIn, c: Container = Depends(container), user: str = Depends(current_user)):
    return guard(c.ota.start, get_device(c, device_id), body.firmware_id, body.chunk_size, user=user)


@router.post("/devices/{device_id}/ota/abort")
def ota_abort(device_id: str, c: Container = Depends(container), user: str = Depends(current_user)):
    return {"ok": c.ota.abort(device_id, user, c.devices.get(device_id))}


@router.get("/ota/batches")
def list_batches(c: Container = Depends(container)):
    return c.batch_ota.list()


@router.post("/ota/batches")
def start_batch(body: BatchOtaIn, c: Container = Depends(container), user: str = Depends(current_user)):
    devices = get_devices(c, body.device_ids)
    steps = guard(body.steps)
    if not body.allow_type_mismatch:
        from ..services.ota import type_mismatches
        metas = [m for m in (c.firmware.meta(fid) for fid in steps) if m]
        bad = type_mismatches(devices, metas, c.config.ota.type_targets)
        if bad:
            raise HTTPException(400, "Bundle does not fit the device type: " + "; ".join(bad[:10])
                                + (f" (+{len(bad) - 10} more)" if len(bad) > 10 else "")
                                + " — tick 'flash anyway' to override")
    return guard(c.batch_ota.start, user, devices, steps, body.chunk_size, body.concurrency, body.stop_on_failure,
                 body.step_delay_s, body.wait_online, body.online_timeout_s)


@router.post("/ota/batches/{batch_id}/cancel")
def cancel_batch(batch_id: str, c: Container = Depends(container), user: str = Depends(current_user)):
    return {"ok": c.batch_ota.cancel(user, batch_id)}
