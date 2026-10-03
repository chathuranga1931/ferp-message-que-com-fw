"""Message definitions, config keys and device-info keys (from ferp_core)."""

import logging
import threading
from pathlib import Path
from typing import Optional

from ferp_core.config_keys import load_config_keys
from ferp_core.msg_loader import devinfo_keys, group_messages, load_message_defs
from ferp_core.transports import TYPE_NAMES

log = logging.getLogger("ferp.catalog")


class Catalog:
    def __init__(self, messages_dirs: list[Path], config_keys_file: Optional[Path] = None):
        self._dirs = messages_dirs
        self._keys_file = config_keys_file
        self._lock = threading.Lock()
        self.reload()

    def reload(self) -> None:
        msgs_dir = next((d for d in self._dirs if d.is_dir()), None)
        defs = load_message_defs(msgs_dir) if msgs_dir else {}
        keys = load_config_keys(self._keys_file)
        with self._lock:
            self.messages_dir = msgs_dir
            self.messages     = defs
            self.config_keys  = keys
            self.devinfo_keys = devinfo_keys(defs)
        log.info("catalog: %d messages from %s, %d config keys, %d dev-info keys",
                 len(defs), msgs_dir, len(keys), len(self.devinfo_keys))

    def config_key(self, key_id: int):
        return next((k for k in self.config_keys if k.key_id == key_id), None)

    def to_json(self) -> dict:
        with self._lock:
            return {
                "messages_dir": str(self.messages_dir) if self.messages_dir else None,
                "groups": [
                    {"group": g, "messages": [{"name": n, **self.messages[n]} for n in names]}
                    for g, names in group_messages(self.messages).items()
                ],
                "config_keys": [
                    {"key": k.key_id, "name": k.name, "type_id": k.type_id,
                     "type": TYPE_NAMES.get(k.type_id, str(k.type_id)), "group": k.group,
                     "label": k.label, "description": k.description,
                     "min": k.min, "max": k.max, "max_len": k.max_len}
                    for k in self.config_keys
                ],
                "devinfo_keys": [{"key": k, "label": label, "field": field}
                                 for k, label, field in self.devinfo_keys],
            }
