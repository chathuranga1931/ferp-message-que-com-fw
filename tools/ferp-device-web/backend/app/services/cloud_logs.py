"""
Cloud logs — browse, view, follow and download pump logs from the log server
(the web replacement for tools/cloud-udp-monitor/runssh).

Two layouts on the server (both relative to the login's home):

    log.py         <root>/<YYYY-MM-DD>/<SHED>/<SHED>-<PUMP>-<YYYYMMDD>-<HHMM>*.txt
                   API paths "YYYY-MM-DD/SHED/file"
    dump_logs.py  <mac_root>/<YYYY-MM-DD>/<mac>/<mac>-<YYYYMMDD>.txt
                   API paths "@mac/<YYYY-MM-DD>/<mac>/file"

Live follow polls the file size every logs.follow_interval_s and pushes new text
as `logs.data` events {follow_id, path, text, size}. Each follow has a lease the
browser renews (keepalive); abandoned follows stop by themselves. While a file is
quiet a `logs.tick` heartbeat is sent every TICK_S. A followed per-device file
(@mac/...) moves on to the next day's file when it appears (`logs.switch`).
"""

import codecs
import datetime as dt
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
MAC_DIR_RE = re.compile(r"^([0-9a-f]{12}|_invalid)$")
MAC_FILE_DATE_RE = re.compile(r"-(\d{4})(\d{2})(\d{2})\.txt$")
MAC_PREFIX = "@mac/"
_FILE_RE = re.compile(r"^(?P<pump>.+?)-(?P<ymd>\d{8})-(?P<hm>\d{4})(?P<rest>.*)$")
LEASE_S = 45
TICK_S = 10                   # heartbeat while a followed file is quiet
ROLL_CHECK_S = 15             # how often a per-device follow looks for the next day's file
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

    # ── layout roots ──────────────────────────────────────────────────────────

    def _root(self, rel: str = "") -> str:
        r = self._get_config().logs.root.strip("/")
        return f"{r}/{rel}" if rel else r

    def _mac_root(self, rel: str = "") -> str:
        r = self._get_config().logs.mac_root.strip("/")
        return f"{r}/{rel}" if rel else r

    def _resolve(self, path: str) -> str:
        """API path → path on the source (validated)."""
        if path.startswith(MAC_PREFIX):
            parts = path[len(MAC_PREFIX):].split("/")
            if len(parts) != 3 or not DATE_RE.match(parts[0]) or not MAC_DIR_RE.match(parts[1]):
                raise ValueError(f"Bad log path {path!r}")
            _check_name(parts[2])
            return self._mac_root("/".join(parts))
        _check_path(path)
        return self._root(path)

    def test(self) -> dict:
        t0 = time.time()
        out = {"ok": True}
        try:
            dates = self.dates(fresh=True)
            out.update(dates=len(dates), latest=dates[0] if dates else None)
        except FileNotFoundError:
            out.update(dates=0, latest=None)
        try:
            out["devices"] = len(self.mac_devices(fresh=True))
        except FileNotFoundError:
            out["devices"] = 0
        if not out.get("dates") and not out.get("devices"):
            self.source().listdir("")                       # proves the login works; folders just empty / missing
        out["ms"] = int((time.time() - t0) * 1000)
        return out

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
        return sorted((i["name"] for i in self._listdir(self._root(), fresh) if i["is_dir"] and DATE_RE.match(i["name"])),
                      reverse=True)

    def sheds(self, date: str, fresh: bool = False) -> list[str]:
        _check_date(date)
        return sorted(i["name"] for i in self._listdir(self._root(date), fresh) if i["is_dir"])

    def files(self, date: str, shed: str, fresh: bool = False) -> list[dict]:
        _check_date(date)
        _check_name(shed)
        out = []
        for i in self._listdir(self._root(f"{date}/{shed}"), fresh):
            if i["is_dir"]:
                continue
            out.append({"name": i["name"], "path": f"{date}/{shed}/{i['name']}", "size": i["size"],
                        "mtime": i["mtime"], **parse_log_name(shed, i["name"])})
        return sorted(out, key=lambda f: (f["pump"] or "~", f["time"] or "", f["name"]))

    # ── by device (dump_logs.py) ─────────────────────────────────────────────

    def _mac_index(self, fresh: bool = False) -> dict[str, list[str]]:
        """{mac: [day, ...newest first]} over the most recent `mac_scan_days` day folders."""
        days = sorted((i["name"] for i in self._listdir(self._mac_root(), fresh)
                       if i["is_dir"] and DATE_RE.match(i["name"])), reverse=True)
        index: dict[str, list[str]] = {}
        for day in days[: self._get_config().logs.mac_scan_days]:
            for i in self._listdir(self._mac_root(day), fresh):
                if i["is_dir"] and MAC_DIR_RE.match(i["name"]):
                    index.setdefault(i["name"], []).append(day)
        return index

    def mac_devices(self, fresh: bool = False) -> list[dict]:
        """Devices with logs in the scanned days: [{mac, last_date, days}], most recently active first."""
        return sorted(({"mac": mac, "last_date": days[0], "days": len(days)}
                       for mac, days in self._mac_index(fresh).items()),
                      key=lambda d: (d["last_date"], d["mac"]), reverse=True)

    def mac_files(self, mac: str, fresh: bool = False) -> list[dict]:
        mac = mac.lower()
        if not MAC_DIR_RE.match(mac):
            raise ValueError(f"Bad MAC {mac!r}")
        out = []
        for day in self._mac_index(fresh).get(mac, []):
            for i in self._listdir(self._mac_root(f"{day}/{mac}"), fresh):
                if i["is_dir"]:
                    continue
                out.append({"name": i["name"], "path": f"{MAC_PREFIX}{day}/{mac}/{i['name']}", "size": i["size"],
                            "mtime": i["mtime"], "date": day, "pump": None, "time": None})
        return sorted(out, key=lambda f: (f["date"], f["name"]), reverse=True)

    def zip_mac(self, mac: str, since: Optional[str], until: Optional[str]) -> tuple[str, Path]:
        for d in (since, until):
            if d:
                _check_date(d)
        files = [f for f in self.mac_files(mac, fresh=True)
                 if (not since or (f["date"] or "") >= since) and (not until or (f["date"] or "") <= until)]
        if not files:
            raise FileNotFoundError("No log files for this device in that range")
        return self._zip(files, f"{mac}/", f"{mac}{'-' + since if since else ''}{'_' + until if until else ''}.zip")
        # (zip entries are flat per device: <mac>/<mac>-YYYYMMDD.txt)

    def _zip(self, files: list[dict], folder: str, name: str) -> tuple[str, Path]:
        src = self.source()
        fd, tmp = tempfile.mkstemp(suffix=".zip", prefix="ferp_logs_")
        os.close(fd)                       # Windows: zipfile re-opens the path itself
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zf:
            for f in files:
                real = self._resolve(f["path"])
                zf.writestr(f"{folder}{f['name']}", src.read(real, 0, f["size"] or src.stat(real)["size"]))
        self._console.log("info", f"Logs zipped: {name} ({len(files)} files)")
        return name, Path(tmp)

    # ── reading ───────────────────────────────────────────────────────────────

    def read(self, path: str, offset: Optional[int], length: int, tail: bool) -> dict:
        """Read a window of a file. tail=True → the last `length` bytes."""
        real = self._resolve(path)
        length = min(max(length, 1), MAX_READ)
        src = self.source()
        size = src.stat(real)["size"]
        if tail or offset is None:
            offset = max(0, size - length)
        offset = min(max(offset, 0), size)
        data = src.read(real, offset, min(length, size - offset))
        if offset > 0 and tail:                        # start on a line boundary
            nl = data.find(b"\n")
            if 0 <= nl < len(data) - 1:
                offset += nl + 1
                data = data[nl + 1:]
        return {"path": path, "size": size, "offset": offset, "end": offset + len(data),
                "text": data.decode("utf-8", errors="replace")}

    def open_file(self, path: str) -> tuple[str, bytes]:
        real = self._resolve(path)
        src = self.source()
        size = src.stat(real)["size"]
        return path.rsplit("/", 1)[-1], src.read(real, 0, size)

    def zip_files(self, date: str, shed: str, pump: Optional[str]) -> tuple[str, Path]:
        """Zip all files of a shed (optionally one pump) for a day into a temp file."""
        files = [f for f in self.files(date, shed, fresh=True) if not pump or f["pump"] == pump]
        if not files:
            raise FileNotFoundError("No matching log files")
        return self._zip(files, f"{shed}/{date}/", f"{shed}-{pump + '-' if pump else ''}{date}.zip")

    # ── live follow ───────────────────────────────────────────────────────────

    def follow(self, path: str, from_end_bytes: int, from_offset: Optional[int] = None) -> dict:
        real = self._resolve(path)
        if from_offset is not None:          # viewer already has text up to from_offset
            fid = uuid.uuid4().hex[:10]
            self._follows[fid] = self._new_follow(path, real, from_offset)
            return {"follow_id": fid, "path": path, "size": from_offset, "text": "", "offset": from_offset}
        first = self.read(path, None, max(from_end_bytes, 1), tail=True) if from_end_bytes else None
        size = first["size"] if first else self.source().stat(real)["size"]
        fid = uuid.uuid4().hex[:10]
        self._follows[fid] = self._new_follow(path, real, size)
        return {"follow_id": fid, "path": path, "size": size,
                "text": first["text"] if first else "", "offset": first["offset"] if first else size}

    @staticmethod
    def _new_follow(path: str, real: str, pos: int) -> dict:
        now = time.time()
        return {"path": path, "real": real, "pos": pos, "lease": now + LEASE_S, "errors": 0,
                "dec": codecs.getincrementaldecoder("utf-8")("replace"),   # keeps split multi-byte chars intact
                "last_event": now, "next_roll": now + ROLL_CHECK_S}

    def _next_day_path(self, path: str) -> Optional[str]:
        """@mac/2026-10-04/<mac>/<mac>-20261004.txt → the same device's file for 2026-10-05."""
        if not path.startswith(MAC_PREFIX):
            return None
        day, mac, _ = path[len(MAC_PREFIX):].split("/")
        nxt = dt.date.fromisoformat(day) + dt.timedelta(days=1)
        return f"{MAC_PREFIX}{nxt.isoformat()}/{mac}/{mac}-{nxt.strftime('%Y%m%d')}.txt"

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
                    size = self.source().stat(f["real"])["size"]
                    if size < f["pos"]:                    # file truncated / rotated
                        f["pos"] = 0
                    if size > f["pos"]:
                        n = min(size - f["pos"], MAX_READ)
                        data = self.source().read(f["real"], f["pos"], n)
                        f["pos"] += len(data)
                        text = f["dec"].decode(data)
                        if text:
                            self._bus.publish({"type": "logs.data", "follow_id": fid, "path": f["path"],
                                               "text": text, "size": size})
                        f["last_event"] = now
                    elif now >= f["next_roll"]:
                        f["next_roll"] = now + ROLL_CHECK_S
                        nxt = self._next_day_path(f["path"])
                        if nxt:
                            try:
                                real = self._resolve(nxt)
                                self.source().stat(real)       # FileNotFoundError until the device logs that day
                                f.update(path=nxt, real=real, pos=0, last_event=now,
                                         dec=codecs.getincrementaldecoder("utf-8")("replace"))
                                self._bus.publish({"type": "logs.switch", "follow_id": fid, "path": nxt,
                                                   "name": nxt.rsplit("/", 1)[-1]})
                                continue
                            except FileNotFoundError:
                                pass
                    if now - f["last_event"] >= TICK_S:
                        f["last_event"] = now
                        self._bus.publish({"type": "logs.tick", "follow_id": fid, "size": size})
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
