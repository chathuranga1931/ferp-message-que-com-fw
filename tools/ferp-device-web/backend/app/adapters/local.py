"""Local adapters: JSON files + a directory on this PC, in-process event bus."""

import asyncio
import json
import os
import threading
import uuid
from pathlib import Path
from typing import Optional

from ..models import AppConfig, Device, DeviceIn


def _read_json(path: Path, default):
    try:
        return json.loads(path.read_text(encoding="utf-8-sig"))   # tolerate a BOM (Notepad / PowerShell edits)
    except FileNotFoundError:
        return default


def _write_json_atomic(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2), encoding="utf-8")
    os.replace(tmp, path)


class JsonConfigStore:
    """App config in a JSON file; created from the defaults file on first run."""

    def __init__(self, path: Path, defaults_path: Optional[Path] = None):
        self._path, self._defaults = path, defaults_path
        self._lock = threading.Lock()

    def load(self) -> AppConfig:
        with self._lock:
            if not self._path.exists():
                seed = _read_json(self._defaults, {}) if self._defaults else {}
                cfg = AppConfig.model_validate(seed)
                _write_json_atomic(self._path, cfg.model_dump())
                return cfg
            return AppConfig.model_validate(_read_json(self._path, {}))

    def save(self, cfg: AppConfig) -> None:
        with self._lock:
            _write_json_atomic(self._path, cfg.model_dump())


class JsonDeviceRepository:
    """Device registry in a JSON file ({"devices": [...]}); seeded on first run."""

    def __init__(self, path: Path, seed_path: Optional[Path] = None):
        self._path = path
        self._lock = threading.Lock()
        if not path.exists():
            seed = _read_json(seed_path, {}) if seed_path else {}
            devices = [Device(id=uuid.uuid4().hex[:8], **DeviceIn.model_validate(d).model_dump())
                       for d in seed.get("devices", [])]
            self._save(devices)

    def _load(self) -> list[Device]:
        return [Device.model_validate(d) for d in _read_json(self._path, {}).get("devices", [])]

    def _save(self, devices: list[Device]) -> None:
        _write_json_atomic(self._path, {"devices": [d.model_dump() for d in devices]})

    def list(self) -> list[Device]:
        with self._lock:
            return self._load()

    def get(self, device_id: str) -> Optional[Device]:
        return next((d for d in self.list() if d.id == device_id), None)

    def add(self, dev: DeviceIn) -> Device:
        with self._lock:
            devices = self._load()
            new = Device(id=uuid.uuid4().hex[:8], **dev.model_dump())
            devices.append(new)
            self._save(devices)
            return new

    def update(self, device_id: str, dev: DeviceIn) -> Optional[Device]:
        with self._lock:
            devices = self._load()
            for i, d in enumerate(devices):
                if d.id == device_id:
                    devices[i] = Device(id=device_id, **dev.model_dump())
                    self._save(devices)
                    return devices[i]
            return None

    def delete(self, device_id: str) -> bool:
        with self._lock:
            devices = self._load()
            kept = [d for d in devices if d.id != device_id]
            if len(kept) == len(devices):
                return False
            self._save(kept)
            return True


class JsonDocumentStore:
    """One JSON file per collection: {doc_id: doc}."""

    def __init__(self, root: Path):
        self._root = root
        self._lock = threading.Lock()

    def _path(self, collection: str) -> Path:
        if not collection.replace("-", "").replace("_", "").isalnum():
            raise ValueError(f"bad collection name {collection!r}")
        return self._root / f"{collection}.json"

    def put(self, collection: str, doc_id: str, doc: dict) -> None:
        with self._lock:
            p = self._path(collection)
            data = _read_json(p, {})
            data[doc_id] = doc
            _write_json_atomic(p, data)

    def get(self, collection: str, doc_id: str) -> Optional[dict]:
        with self._lock:
            return _read_json(self._path(collection), {}).get(doc_id)

    def list(self, collection: str) -> list[dict]:
        with self._lock:
            return list(_read_json(self._path(collection), {}).values())

    def delete(self, collection: str, doc_id: str) -> bool:
        with self._lock:
            p = self._path(collection)
            data = _read_json(p, {})
            if doc_id not in data:
                return False
            del data[doc_id]
            _write_json_atomic(p, data)
            return True


class FileBlobStore:
    """Blobs as files under a root directory; keys use '/' separators."""

    def __init__(self, root: Path):
        self._root = root.resolve()
        self._root.mkdir(parents=True, exist_ok=True)

    def _path(self, key: str) -> Path:
        p = (self._root / key).resolve()
        if self._root not in p.parents:
            raise KeyError(key)
        return p

    def put(self, key: str, data: bytes) -> None:
        p = self._path(key)
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp = p.with_suffix(p.suffix + ".tmp")
        tmp.write_bytes(data)
        os.replace(tmp, p)

    def get(self, key: str) -> bytes:
        try:
            return self._path(key).read_bytes()
        except FileNotFoundError:
            raise KeyError(key)

    def delete(self, key: str) -> None:
        self._path(key).unlink(missing_ok=True)

    def list(self, prefix: str = "") -> list[str]:
        return sorted(p.relative_to(self._root).as_posix()
                      for p in self._root.rglob("*")
                      if p.is_file() and not p.name.endswith(".tmp")
                      and p.relative_to(self._root).as_posix().startswith(prefix))


class MemoryEventBus:
    """Fans events out to asyncio queues; publish() is safe from any thread."""

    def __init__(self, queue_size: int = 2000):
        self._subs: set[asyncio.Queue] = set()
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._size = queue_size

    def bind_loop(self, loop: asyncio.AbstractEventLoop) -> None:
        self._loop = loop

    def publish(self, event: dict) -> None:
        if self._loop is not None and not self._loop.is_closed():
            self._loop.call_soon_threadsafe(self._fanout, event)

    def _fanout(self, event: dict) -> None:
        for q in list(self._subs):
            if q.full():          # slow client: drop its oldest event rather than block everyone
                q.get_nowait()
            q.put_nowait(event)

    def subscribe(self) -> asyncio.Queue:
        q: asyncio.Queue = asyncio.Queue(maxsize=self._size)
        self._subs.add(q)
        return q

    def unsubscribe(self, q: asyncio.Queue) -> None:
        self._subs.discard(q)
