"""Saved console history and audit log — query and export."""

import csv
import io
import json
from datetime import datetime

from fastapi import Depends, Query
from fastapi.responses import Response

from ..container import Container
from .deps import container, protected

router = protected()


def _console_filters(device, level, q, since, until, before_id, limit) -> dict:
    return {"device": device or None, "level": [l for l in (level or "").split(",") if l] or None,
            "q": q or None, "since": since, "until": until, "before_id": before_id, "limit": limit}


@router.get("/history/console")
def console_history(device: str | None = None, level: str | None = None, q: str | None = None,
                    since: float | None = None, until: float | None = None, before_id: int | None = None,
                    limit: int = Query(500, ge=1, le=5000), c: Container = Depends(container)):
    return c.history.query_console(**_console_filters(device, level, q, since, until, before_id, limit))


@router.get("/history/console/export")
def console_export(device: str | None = None, level: str | None = None, q: str | None = None,
                   since: float | None = None, until: float | None = None,
                   limit: int = Query(50000, ge=1, le=200000), c: Container = Depends(container)):
    rows = c.history.query_console(**_console_filters(device, level, q, since, until, None, limit))
    lines = [f"{datetime.fromtimestamp(r['ts']).isoformat(sep=' ', timespec='milliseconds')} "
             f"[{r['level']}]{' ' + r['device'] if r['device'] else ''} {r['text']}" for r in reversed(rows)]
    return _download("\n".join(lines) + "\n", "console", "txt", "text/plain")


@router.get("/history/audit")
def audit_history(device: str | None = None, action: str | None = None, q: str | None = None,
                  since: float | None = None, until: float | None = None, before_id: int | None = None,
                  limit: int = Query(500, ge=1, le=5000), c: Container = Depends(container)):
    return c.audit.query(device=device or None, action=action or None, q=q or None, since=since,
                         until=until, before_id=before_id, limit=limit)


@router.get("/history/audit/export")
def audit_export(device: str | None = None, action: str | None = None, q: str | None = None,
                 since: float | None = None, until: float | None = None,
                 limit: int = Query(50000, ge=1, le=200000), c: Container = Depends(container)):
    rows = c.audit.query(device=device or None, action=action or None, q=q or None, since=since,
                         until=until, limit=limit)
    buf = io.StringIO()
    w = csv.writer(buf)
    w.writerow(["time", "user", "action", "device", "detail"])
    for r in reversed(rows):
        w.writerow([datetime.fromtimestamp(r["ts"]).isoformat(sep=" ", timespec="seconds"), r["user"],
                    r["action"], r["device_label"] or "", json.dumps(r["detail"])])
    return _download(buf.getvalue(), "audit", "csv", "text/csv")


def _download(body: str, name: str, ext: str, media: str) -> Response:
    fname = f"ferp-{name}-{datetime.now():%Y%m%d-%H%M%S}.{ext}"
    return Response(body, media_type=f"{media}; charset=utf-8",
                    headers={"Content-Disposition": f'attachment; filename="{fname}"'})
