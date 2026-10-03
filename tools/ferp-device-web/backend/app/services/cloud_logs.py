"""
Cloud logs — browse, view, follow and download pump logs from the log server
(the web replacement for tools/cloud-udp-monitor/runssh).

Layout on the server:   <root>/<YYYY-MM-DD>/<SHED>/<SHED>-<PUMP>-<YYYYMMDD>-<HHMM>*.txt

Live follow polls the file size every logs.follow_interval_s and pushes new text
as `logs.data` events {follow_id, path, text, size}. Each follow has a lease the
browser renews (keepalive); abandoned follows stop by themselves.
"""

import logging
import os
import re
import tempfile
import threading
import time
import uuid
import zipfile
from pathlib import Path
from typing import Callable, Optional

from ..adapters.log_sources import make_source
from ..models import AppConfig, LogsConfig
from ..ports import EventBus, LogSource
from .console import Console

log = logging.getLogger("ferp.logs")

DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
_FILE_RE = re.compile(r"^(?P<pump>.+?)-(?P<ymd>\d{8})-(?P<hm>\d{4})(?P<rest>.*)$")
LEASE_S = 45
MAX_READ = 2_000_000          # bytes per read request
_LIST_CACHE_S = 10


def parse_log_name(shed: str, filename: str) -> dict:
    """'YAKKALA-D04-20260609-1816-x.txt' (shed YAKKALA) → pump D04, time 18:16."""
    stem = filename[:-4] if filename.lower().endswith(".txt") else filename
    rest = stem[len(shed) + 1:] if stem.startswith(shed + "-") else stem
    m = _FILE_RE.match(rest)
    if not m:
        return {"pump": None, "time": None}
    hm = m.group("hm")
    return {"pump": m.group("pump"), "time": f"{hm[:2]}:{hm[2:]}"}


class CloudLogs:
    def __init__(self, get_config: Callable[[], AppConfig], data_dir: Path, bus: EventBus, console: Console):
        self._get_config, self._data_dir, self._bus, self._console = get_config, data_dir, bus, console
        self._source: Optional[LogSource] = None
        self._source_cfg: Optional[LogsConfig] = None
        self._lock = threading.Lock()
        self._cache: dict[str, tuple[float, list]] = {}
        self._follows: dict[str, dict] = {}
        threading.Thread(target=self._follow_loop, daemon=True, name="logs-follow").start()

    # ── source (rebuilt when settings change) ─────────────────────────────────

    def source(self) -> LogSource:
        cfg = self._get_config().logs
        with self._lock:
            if self._source is None or cfg != self._source_cfg:
                if self._source is not None:
                    self._source.close()
                self._source, self._source_cfg = make_source(cfg, self._data_dir), cfg
                self._cache.clear()
            return self._source

    def test(self) -> dict:
        t0 = time.time()
        dates = self.dates(fresh=True)
        return {"ok": True, "ms": int((time.time() - t0) * 1000), "dates": len(dates),
                "latest": dates[0] if dates else None}

    def _listdir(self, rel: str, fresh: bool = False) -> list[dict]:
        now = time.time()
        hit = self._cache.get(rel)
        if hit and not fresh and now - hit[0] < _LIST_CACHE_S:
            return hit[1]
        items = self.source().listdir(rel)
        self._cache[rel] = (now, items)
        return items

    # ── browsing ──────────────────────────────────────────────────────────────

    def dates(self, fresh: bool = False) -> list[str]:
        return sorted((i["name"] for i in self._listdir("", fresh) if i["is_dir"] and DATE_RE.match(i["name"])),
                      reverse=True)

    def sheds(self, date: str, fresh: bool = False) -> list[str]:
        _check_date(date)
        return sorted(i["name"] for i in self._listdir(date, fresh) if i["is_dir"])

    def files(self, date: str, shed: str, fresh: bool = False) -> list[dict]:
        _check_date(date)
        _check_name(shed)
        out = []
        for i in self._listdir(f"{date}/{shed}", fresh):
            if i["is_dir"]:
                continue
            out.append({"name": i["name"], "path": f"{date}/{shed}/{i['name']}", "size": i["size"],
                        "mtime": i["mtime"], **parse_log_name(shed, i["name"])})
        return sorted(out, key=lambda f: (f["pump"] or "~", f["time"] or "", f["name"]))

    # ── reading ───────────────────────────────────────────────────────────────

    def read(self, path: str, offset: Optional[int], length: int, tail: bool) -> dict:
        """Read a window of a file. tail=True → the last `length` bytes."""
        _check_path(path)
        length = min(max(length, 1), MAX_READ)
        src = self.source()
        size = src.stat(path)["size"]
        if tail or offset is None:
            offset = max(0, size - length)
        offset = min(max(offset, 0), size)
        data = src.read(path, offset, min(length, size - offset))
        if offset > 0 and tail:                        # start on a line boundary
            nl = data.find(b"\n")
            if 0 <= nl < len(data) - 1:
                offset += nl + 1
                data = data[nl + 1:]
        return {"path": path, "size": size, "offset": offset, "end": offset + len(data),
                "text": data.decode("utf-8", errors="replace")}

    def open_file(self, path: str) -> tuple[str, bytes]:
        _check_path(path)
        src = self.source()
        size = src.stat(path)["size"]
        return path.rsplit("/", 1)[-1], src.read(path, 0, size)

    def zip_files(self, date: str, shed: str, pump: Optional[str]) -> tuple[str, Path]:
        """Zip all files of a shed (optionally one pump) for a day into a temp file."""
        files = [f for f in self.files(date, shed, fresh=True) if not pump or f["pump"] == pump]
        if not files:
            raise FileNotFoundError("No matching log files")
        src = self.source()
        fd, tmp = tempfile.mkstemp(suffix=".zip", prefix="ferp_logs_")
        os.close(fd)                       # Windows: zipfile re-opens the path itself
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zf:
            for f in files:
                zf.writestr(f"{shed}/{date}/{f['name']}", src.read(f["path"], 0, f["size"] or src.stat(f["path"])["size"]))
        name = f"{shed}-{pump + '-' if pump else ''}{date}.zip"
        self._console.log("info", f"Logs zipped: {name} ({len(files)} files)")
        return name, Path(tmp)

    # ── live follow ───────────────────────────────────────────────────────────

    def follow(self, path: str, from_end_bytes: int, from_offset: Optional[int] = None) -> dict:
        _check_path(path)
        if from_offset is not None:          # viewer already has text up to from_offset
            fid = uuid.uuid4().hex[:10]
            self._follows[fid] = {"path": path, "pos": from_offset, "lease": time.time() + LEASE_S, "errors": 0}
            return {"follow_id": fid, "path": path, "size": from_offset, "text": "", "offset": from_offset}
        first = self.read(path, None, max(from_end_bytes, 1), tail=True) if from_end_bytes else None
        size = first["size"] if first else self.source().stat(path)["size"]
        fid = uuid.uuid4().hex[:10]
        self._follows[fid] = {"path": path, "pos": size, "lease": time.time() + LEASE_S, "errors": 0}
        return {"follow_id": fid, "path": path, "size": size,
                "text": first["text"] if first else "", "offset": first["offset"] if first else size}

    def keepalive(self, fid: str) -> bool:
        f = self._follows.get(fid)
        if f:
            f["lease"] = time.time() + LEASE_S
        return f is not None

    def unfollow(self, fid: str) -> bool:
        return self._follows.pop(fid, None) is not None

    def _follow_loop(self) -> None:
        while True:
            time.sleep(self._get_config().logs.follow_interval_s)
            now = time.time()
            for fid, f in list(self._follows.items()):
                if now > f["lease"]:
                    self._follows.pop(fid, None)
                    continue
                try:
                    size = self.source().stat(f["path"])["size"]
                    if size < f["pos"]:                    # file truncated / rotated
                        f["pos"] = 0
                    if size > f["pos"]:
                        n = min(size - f["pos"], MAX_READ)
                        data = self.source().read(f["path"], f["pos"], n)
                        f["pos"] += len(data)
                        self._bus.publish({"type": "logs.data", "follow_id": fid, "path": f["path"],
                                           "text": data.decode("utf-8", errors="replace"), "size": size})
                    f["errors"] = 0
                except Exception as exc:
                    f["errors"] += 1
                    if f["errors"] in (1, 10):
                        self._bus.publish({"type": "logs.error", "follow_id": fid, "error": str(exc)})
                    log.warning("follow %s: %s", f["path"], exc)

    def close(self) -> None:
        self._follows.clear()
        with self._lock:
            if self._source is not None:
                self._source.close()


def _check_date(d: str) -> None:
    if not DATE_RE.match(d):
        raise ValueError(f"Bad date {d!r} (expected YYYY-MM-DD)")


def _check_name(n: str) -> None:
    if not n or "/" in n or "\\" in n or n in (".", ".."):
        raise ValueError(f"Bad name {n!r}")


def _check_path(p: str) -> None:
    parts = p.split("/")
    if len(parts) != 3:
        raise ValueError(f"Bad log path {p!r}")
    _check_date(parts[0]); _check_name(parts[1]); _check_name(parts[2])
