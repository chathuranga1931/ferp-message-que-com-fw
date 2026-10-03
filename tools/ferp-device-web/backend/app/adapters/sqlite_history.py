"""HistoryStore on SQLite (stdlib) — console lines and audit log in data/history.db."""

import json
import sqlite3
import threading
from pathlib import Path
from typing import Any, Optional

_SCHEMA = """
CREATE TABLE IF NOT EXISTS console (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    ts      REAL NOT NULL,
    level   TEXT NOT NULL,
    device  TEXT,
    topic   TEXT,
    text    TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS console_ts     ON console(ts);
CREATE INDEX IF NOT EXISTS console_device ON console(device, ts);

CREATE TABLE IF NOT EXISTS audit (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    ts           REAL NOT NULL,
    user         TEXT NOT NULL,
    action       TEXT NOT NULL,
    device_id    TEXT,
    device_label TEXT,
    detail       TEXT NOT NULL DEFAULT '{}'
);
CREATE INDEX IF NOT EXISTS audit_ts     ON audit(ts);
CREATE INDEX IF NOT EXISTS audit_device ON audit(device_id, ts);
CREATE INDEX IF NOT EXISTS audit_action ON audit(action, ts);
"""


class SqliteHistoryStore:
    def __init__(self, path: Path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self._db = sqlite3.connect(path, check_same_thread=False)
        self._db.row_factory = sqlite3.Row
        self._lock = threading.Lock()
        with self._lock:
            self._db.execute("PRAGMA journal_mode=WAL")
            self._db.execute("PRAGMA synchronous=NORMAL")
            self._db.executescript(_SCHEMA)
            self._db.commit()

    # ── console ───────────────────────────────────────────────────────────────

    def append_console(self, entries: list[dict]) -> None:
        if not entries:
            return
        with self._lock:
            self._db.executemany(
                "INSERT INTO console (ts, level, device, topic, text) VALUES (?, ?, ?, ?, ?)",
                [(e["ts"], e["level"], e.get("device"), e.get("topic"), e["text"]) for e in entries])
            self._db.commit()

    def query_console(self, device: Optional[str] = None, level: Optional[list[str]] = None,
                      q: Optional[str] = None, since: Optional[float] = None, until: Optional[float] = None,
                      before_id: Optional[int] = None, limit: int = 500, **_: Any) -> list[dict]:
        where, args = self._common(device, "device", q, "text", since, until, before_id)
        if level:
            where.append(f"level IN ({','.join('?' * len(level))})")
            args += level
        sql = f"SELECT * FROM console {self._w(where)} ORDER BY id DESC LIMIT ?"
        with self._lock:
            return [dict(r) for r in self._db.execute(sql, [*args, limit])]

    # ── audit ─────────────────────────────────────────────────────────────────

    def append_audit(self, entry: dict) -> None:
        with self._lock:
            self._db.execute(
                "INSERT INTO audit (ts, user, action, device_id, device_label, detail) VALUES (?, ?, ?, ?, ?, ?)",
                (entry["ts"], entry["user"], entry["action"], entry.get("device_id"),
                 entry.get("device_label"), json.dumps(entry.get("detail") or {})))
            self._db.commit()

    def query_audit(self, device: Optional[str] = None, action: Optional[str] = None,
                    q: Optional[str] = None, since: Optional[float] = None, until: Optional[float] = None,
                    before_id: Optional[int] = None, limit: int = 500, **_: Any) -> list[dict]:
        where, args = self._common(device, "device_id", q, "action || ' ' || detail || ' ' || IFNULL(device_label, '')",
                                   since, until, before_id)
        if action:   # prefix match: "ota" matches ota.start, ota.result, ...
            where.append("(action = ? OR action LIKE ?)")
            args += [action, f"{action}.%"]
        sql = f"SELECT * FROM audit {self._w(where)} ORDER BY id DESC LIMIT ?"
        with self._lock:
            rows = [dict(r) for r in self._db.execute(sql, [*args, limit])]
        for r in rows:
            r["detail"] = json.loads(r["detail"] or "{}")
        return rows

    # ── maintenance ───────────────────────────────────────────────────────────

    def prune(self, console_before: float, audit_before: float) -> None:
        with self._lock:
            self._db.execute("DELETE FROM console WHERE ts < ?", (console_before,))
            self._db.execute("DELETE FROM audit WHERE ts < ?", (audit_before,))
            self._db.commit()

    @staticmethod
    def _common(device, device_col, q, text_col, since, until, before_id):
        where, args = [], []
        if device:
            where.append(f"{device_col} = ?"); args.append(device)
        if q:
            where.append(f"{text_col} LIKE ?"); args.append(f"%{q}%")
        if since is not None:
            where.append("ts >= ?"); args.append(since)
        if until is not None:
            where.append("ts <= ?"); args.append(until)
        if before_id is not None:
            where.append("id < ?"); args.append(before_id)
        return where, args

    @staticmethod
    def _w(where: list[str]) -> str:
        return ("WHERE " + " AND ".join(where)) if where else ""
