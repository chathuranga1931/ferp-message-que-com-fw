"""Saved message commands (favourites) and recently sent commands (from the audit log)."""

import json
import time
import uuid

from ..ports import DocumentStore
from .audit import Audit

_COLLECTION = "favorites"


class Favorites:
    def __init__(self, docs: DocumentStore, audit: Audit):
        self._docs, self._audit = docs, audit

    def list(self) -> list[dict]:
        return sorted(self._docs.list(_COLLECTION), key=lambda f: f["name"].lower())

    def add(self, user: str, name: str, msg: str, data: dict) -> dict:
        fav = {"id": uuid.uuid4().hex[:10], "name": name, "msg": msg, "data": data,
               "created": time.time(), "created_by": user}
        self._docs.put(_COLLECTION, fav["id"], fav)
        self._audit.record(user, "favorite.add", name=name, msg=msg)
        return fav

    def delete(self, user: str, fid: str) -> bool:
        ok = self._docs.delete(_COLLECTION, fid)
        if ok:
            self._audit.record(user, "favorite.delete", id=fid)
        return ok

    def recent(self, limit: int = 20) -> list[dict]:
        """Distinct recently sent (msg, data) pairs, newest first."""
        out, seen = [], set()
        for e in self._audit.query(action="message.send", limit=300):
            d = e["detail"]
            sig = (d.get("msg"), json.dumps(d.get("data"), sort_keys=True))
            if sig in seen or not d.get("msg"):
                continue
            seen.add(sig)
            out.append({"msg": d["msg"], "data": d.get("data") or {}, "ts": e["ts"],
                        "device_label": e.get("device_label")})
            if len(out) >= limit:
                break
        return out
