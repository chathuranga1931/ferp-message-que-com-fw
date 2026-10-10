"""
Audit log — who changed what on which device. Recorded actions:

    settings.save  mqtt.connect  mqtt.disconnect
    device.add  device.update  device.delete
    message.send  config.write
    ota.start  ota.result  ota.abort  ota.batch.start  ota.batch.cancel
    firmware.upload  firmware.delete  firmware.activate  firmware.archive
    snapshot.create  snapshot.delete  snapshot.apply
    favorite.add  favorite.delete  system.restart

Values of secret-looking config keys (PASSWORD / SECRET / TOKEN / KEY in the
name) are masked before they are stored.
"""

import logging
import threading
import time
from typing import Callable, Optional

from ..models import Device
from ..ports import EventBus, HistoryStore

log = logging.getLogger("ferp.audit")

_SECRET_MARKERS = ("PASSWORD", "SECRET", "TOKEN", "_KEY")


def is_secret(key_name: str) -> bool:
    n = key_name.upper()
    return any(m in n for m in _SECRET_MARKERS)


class Audit:
    def __init__(self, history: HistoryStore, bus: EventBus, retention: Callable[[], tuple[int, int]]):
        self._history, self._bus, self._retention = history, bus, retention
        threading.Thread(target=self._pruner, daemon=True, name="history-pruner").start()

    def record(self, user: str, action: str, device: Optional[Device] = None, **detail) -> None:
        entry = {"ts": time.time(), "user": user, "action": action,
                 "device_id": device.id if device else None,
                 "device_label": device.label if device else None, "detail": detail}
        try:
            self._history.append_audit(entry)
        except Exception:
            log.exception("audit write failed: %s", entry)
            return
        self._bus.publish({"type": "audit", "entry": entry})

    def query(self, **filters) -> list[dict]:
        return self._history.query_audit(**filters)

    def _pruner(self) -> None:
        while True:
            try:
                console_days, audit_days = self._retention()
                now = time.time()
                self._history.prune(now - console_days * 86400, now - audit_days * 86400)
            except Exception:
                log.exception("history prune failed")
            time.sleep(3600)
