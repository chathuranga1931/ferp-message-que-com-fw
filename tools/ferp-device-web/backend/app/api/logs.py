"""Cloud logs: browse, view, follow, download."""

from fastapi import Depends, HTTPException, Query
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import FileResponse, Response
from starlette.background import BackgroundTask

from ..container import Container
from ..models import LogFollowIn
from .deps import container, need, protected

router = protected()


async def _run(fn, *args):
    """Run a (blocking, network) call off the event loop; map errors to HTTP."""
    try:
        return await run_in_threadpool(fn, *args)
    except ConnectionError as exc:
        raise HTTPException(502, str(exc))
    except FileNotFoundError as exc:
        raise HTTPException(404, f"Not found: {exc}")
    except PermissionError as exc:
        raise HTTPException(403, f"Permission denied: {exc}")
    except ValueError as exc:
        raise HTTPException(400, str(exc))


@router.post("/logs/test", dependencies=[need("logs")])
async def test(c: Container = Depends(container)):
    return await _run(c.logs.test)


@router.get("/logs/dates", dependencies=[need("logs")])
async def dates(fresh: bool = False, c: Container = Depends(container)):
    return await _run(c.logs.dates, fresh)


@router.get("/logs/sheds", dependencies=[need("logs")])
async def sheds(date: str, fresh: bool = False, c: Container = Depends(container)):
    return await _run(c.logs.sheds, date, fresh)


@router.get("/logs/files", dependencies=[need("logs")])
async def files(date: str, shed: str, fresh: bool = False, c: Container = Depends(container)):
    return await _run(c.logs.files, date, shed, fresh)


@router.get("/logs/devices", dependencies=[need("logs")])
async def mac_devices(fresh: bool = False, c: Container = Depends(container)):
    """Device folders written by dump_logs.py."""
    return await _run(c.logs.mac_devices, fresh)


@router.get("/logs/device-files", dependencies=[need("logs")])
async def mac_files(mac: str, fresh: bool = False, until: str | None = None, days: int = Query(10, ge=1, le=62),
                    c: Container = Depends(container)):
    """All files in the scanned days, or with `until` (YYYY-MM-DD) the `days` days ending that day."""
    if until:
        return await _run(c.logs.mac_window, mac, until, days, fresh)
    return await _run(c.logs.mac_files, mac, fresh)


@router.get("/logs/device-zip", dependencies=[need("logs")])
async def mac_zip(mac: str, since: str | None = None, until: str | None = None, c: Container = Depends(container)):
    name, tmp = await _run(c.logs.zip_mac, mac, since or None, until or None)
    return FileResponse(tmp, media_type="application/zip", filename=name,
                        background=BackgroundTask(lambda: tmp.unlink(missing_ok=True)))


@router.get("/logs/read", dependencies=[need("logs")])
async def read(path: str, offset: int | None = None, length: int = Query(262144, ge=1, le=2_000_000),
               tail: bool = False, c: Container = Depends(container)):
    return await _run(c.logs.read, path, offset, length, tail)


@router.get("/logs/download", dependencies=[need("logs")])
async def download(path: str, c: Container = Depends(container)):
    name, data = await _run(c.logs.open_file, path)
    return Response(data, media_type="text/plain; charset=utf-8",
                    headers={"Content-Disposition": f'attachment; filename="{name}"'})


@router.get("/logs/zip", dependencies=[need("logs")])
async def zip_logs(date: str, shed: str, pump: str | None = None, c: Container = Depends(container)):
    name, tmp = await _run(c.logs.zip_files, date, shed, pump or None)
    return FileResponse(tmp, media_type="application/zip", filename=name,
                        background=BackgroundTask(lambda: tmp.unlink(missing_ok=True)))


@router.post("/logs/follow", dependencies=[need("logs")])
async def follow(body: LogFollowIn, c: Container = Depends(container)):
    return await _run(c.logs.follow, body.path, body.from_end_bytes, body.from_offset)


@router.post("/logs/follow/{fid}/keepalive", dependencies=[need("logs")])
def keepalive(fid: str, c: Container = Depends(container)):
    if not c.logs.keepalive(fid):
        raise HTTPException(404, "Follow expired")
    return {"ok": True}


@router.delete("/logs/follow/{fid}", dependencies=[need("logs")])
def unfollow(fid: str, c: Container = Depends(container)):
    return {"ok": c.logs.unfollow(fid)}
