"""
Live console: in-memory ring buffer, every line pushed on the event bus and
(optionally) persisted to the HistoryStore by a background writer that batches
inserts, so MQTT callbacks never wait on disk.
"""

import itertools
import logging
import queue
import threading
import time
from collections import deque
from typing import Callable, Optional

from ..ports import EventBus, HistoryStore

log = logging.getLogger("ferp.console")

LEVELS = ("cmd", "resp", "evt", "info", "warn", "error", "ota")


class Console:
    def __init__(self, bus: EventBus, max_lines: int = 2000,
                 history: Optional[HistoryStore] = None, persist: Callable[[], bool] = lambda: True):
        self._bus = bus
        self._buf: deque = deque(maxlen=max_lines)
        self._ids = itertools.count(1)
        self._lock = threading.Lock()
        self._history, self._persist = history, persist
        self._pending: "queue.Queue[dict]" = queue.Queue()
        if history is not None:
            threading.Thread(target=self._writer, daemon=True, name="console-writer").start()

    def resize(self, max_lines: int) -> None:
        with self._lock:
            if max_lines != self._buf.maxlen:
                self._buf = deque(self._buf, maxlen=max_lines)

    def log(self, level: str, text: str, device: Optional[str] = None,
            topic: Optional[str] = None) -> None:
        entry = {"id": next(self._ids), "ts": time.time(), "level": level,
                 "text": text, "device": device, "topic": topic}
        with self._lock:
            self._buf.append(entry)
        log.info("[%s]%s %s", level, f" {device}" if device else "", text)
        self._bus.publish({"type": "console", "entry": entry})
        if self._history is not None and self._persist():
            self._pending.put(entry)

    def tail(self, limit: int = 500) -> list[dict]:
        with self._lock:
            return list(self._buf)[-limit:]

    def clear(self) -> None:
        """Clears the live view only; history is kept."""
        with self._lock:
            self._buf.clear()
        self._bus.publish({"type": "console.cleared"})

    def _writer(self) -> None:
        while True:
            batch = [self._pending.get()]
            time.sleep(0.5)                     # let a burst accumulate → one transaction
            while not self._pending.empty() and len(batch) < 2000:
                batch.append(self._pending.get_nowait())
            try:
                self._history.append_console(batch)
            except Exception:
                log.exception("console history write failed (%d lines dropped)", len(batch))
