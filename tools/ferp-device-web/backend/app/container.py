"""Composition root — picks adapters from ServerSettings and wires the services."""

import threading
import time
from dataclasses import dataclass
from pathlib import Path

from .adapters.auth import NoAuth, TailscaleHeaderAuth
from .models import AppConfig
from .ports import (AuthProvider, BlobStore, ConfigStore, DeviceRepository, DocumentStore, EventBus,
                    HistoryStore)
from .server_settings import REPO_RELEASES, ServerSettings
from .services.audit import Audit
from .services.catalog import Catalog
from .services.cloud_logs import CloudLogs
from .services.console import Console
from .services.device_ops import DeviceOps
from .services.favorites import Favorites
from .services.fleet import Fleet
from .services.mqtt_hub import MqttHub
from .services.ota import BatchOta, FirmwareLibrary, OtaManager
from .services.snapshots import Snapshots


@dataclass
class Storage:
    config:   ConfigStore
    devices:  DeviceRepository
    blobs:    BlobStore
    docs:     DocumentStore
    history:  HistoryStore
    bus:      EventBus


class Container:
    def __init__(self, settings: ServerSettings):
        self.settings = settings
        self.started = time.time()
        st = self._storage(settings)
        self.config_store, self.devices, self.blobs = st.config, st.devices, st.blobs
        self.docs, self.history, self.bus = st.docs, st.history, st.bus
        self.auth: AuthProvider = self._auth(settings)

        self._cfg_lock = threading.Lock()
        self._config: AppConfig = self.config_store.load()
        cfg = lambda: self.config

        self.console   = Console(self.bus, self._config.console.buffer_lines, self.history,
                                 persist=lambda: self.config.history.persist_console)
        self.audit     = Audit(self.history, self.bus,
                               lambda: (self.config.history.console_retention_days, self.config.history.audit_retention_days))
        self.catalog   = Catalog(settings.messages_dirs)
        self.hub       = MqttHub(lambda: self.config.mqtt, self.console, self.bus)
        self.ops       = DeviceOps(self.hub, self.devices, self.catalog, self.console, self.bus, cfg, self.audit)
        self.fleet     = Fleet(self.hub, self.devices, self.ops, self.catalog, self.docs, self.bus, self.console, cfg)
        self.snapshots = Snapshots(self.docs, self.ops, self.catalog, self.audit)
        self.favorites = Favorites(self.docs, self.audit)
        self.firmware  = FirmwareLibrary(self.blobs, self._bundle_dirs)
        self.ota       = OtaManager(self.firmware, self.console, self.bus, cfg, self.audit)
        self.batch_ota = BatchOta(self.ota, self.firmware, self.console, self.bus, self.audit)
        self.logs      = CloudLogs(cfg, settings.data_dir, self.bus, self.console)

    # ── app config ────────────────────────────────────────────────────────────

    @property
    def config(self) -> AppConfig:
        return self._config

    def save_config(self, cfg: AppConfig) -> None:
        with self._cfg_lock:
            self.config_store.save(cfg)
            self._config = cfg
        self.console.resize(cfg.console.buffer_lines)

    def _bundle_dirs(self) -> list[Path]:
        dirs = [Path(d) for d in self.config.ota.bundle_dirs if d.strip()]
        return dirs or [REPO_RELEASES]

    def shutdown(self) -> None:
        self.ota.abort_all()
        self.hub.disconnect()
        self.fleet.close()
        self.logs.close()

    # ── adapter selection ─────────────────────────────────────────────────────

    @staticmethod
    def _storage(s: ServerSettings) -> Storage:
        if s.storage == "local":
            from .adapters.local import (FileBlobStore, JsonConfigStore, JsonDeviceRepository,
                                         JsonDocumentStore, MemoryEventBus)
            from .adapters.sqlite_history import SqliteHistoryStore
            return Storage(config=JsonConfigStore(s.data_dir / "app-config.json", s.config_defaults),
                           devices=JsonDeviceRepository(s.data_dir / "devices.json", s.devices_seed),
                           blobs=FileBlobStore(s.data_dir / "blobs"),
                           docs=JsonDocumentStore(s.data_dir / "docs"),
                           history=SqliteHistoryStore(s.data_dir / "history.db"),
                           bus=MemoryEventBus())
        # e.g. "gcp" → Firestore + GCS + Pub/Sub, "aws" → DynamoDB + S3 + SNS/SQS
        raise NotImplementedError(f"FERP_STORAGE={s.storage!r} has no adapters yet")

    @staticmethod
    def _auth(s: ServerSettings) -> AuthProvider:
        if s.auth_mode == "none":
            return NoAuth()
        if s.auth_mode == "tailscale":
            return TailscaleHeaderAuth(s.allowed_users)
        raise ValueError(f"Unknown FERP_AUTH_MODE={s.auth_mode!r}")
