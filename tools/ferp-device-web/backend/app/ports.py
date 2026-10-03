"""
Ports — the interfaces services depend on.

Each port has a local adapter today (app/adapters/local). Moving to Google
Cloud or AWS means adding an adapter that implements the same Protocol and
selecting it in app/container.py — services and the API stay unchanged.

    Port              local adapter          cloud candidates
    ────────────────  ─────────────────────  ─────────────────────────────
    ConfigStore       JSON file              Firestore / DynamoDB / SSM
    DeviceRepository  JSON file              Firestore / DynamoDB / Postgres
    BlobStore         directory              GCS / S3
    EventBus          in-process queues      Redis / Pub/Sub / SNS+SQS
    AuthProvider      none / Tailscale hdr   IAP / Cognito / OIDC
    DocumentStore     JSON file/collection   Firestore / DynamoDB
    HistoryStore      SQLite                 BigQuery / Cloud SQL / Timestream
    LogSource         SSH/SFTP or folder     GCS / S3 bucket of log files
"""

import asyncio
from typing import Any, Mapping, Optional, Protocol

from .models import AppConfig, Device, DeviceIn


class ConfigStore(Protocol):
    def load(self) -> AppConfig: ...
    def save(self, cfg: AppConfig) -> None: ...


class DeviceRepository(Protocol):
    def list(self) -> list[Device]: ...
    def get(self, device_id: str) -> Optional[Device]: ...
    def add(self, dev: DeviceIn) -> Device: ...
    def update(self, device_id: str, dev: DeviceIn) -> Optional[Device]: ...
    def delete(self, device_id: str) -> bool: ...


class BlobStore(Protocol):
    def put(self, key: str, data: bytes) -> None: ...
    def get(self, key: str) -> bytes: ...          # raises KeyError if missing
    def delete(self, key: str) -> None: ...
    def list(self, prefix: str = "") -> list[str]: ...


class EventBus(Protocol):
    def publish(self, event: dict) -> None: ...    # must be safe to call from any thread
    def subscribe(self) -> "asyncio.Queue[dict]": ...
    def unsubscribe(self, q: "asyncio.Queue[dict]") -> None: ...


class AuthProvider(Protocol):
    mode: str
    def authenticate(self, headers: Mapping[str, str]) -> Optional[str]: ...  # user id, or None = reject


class DocumentStore(Protocol):
    """Small JSON documents grouped in collections (snapshots, favourites, presence)."""
    def put(self, collection: str, doc_id: str, doc: dict) -> None: ...
    def get(self, collection: str, doc_id: str) -> Optional[dict]: ...
    def list(self, collection: str) -> list[dict]: ...
    def delete(self, collection: str, doc_id: str) -> bool: ...


class HistoryStore(Protocol):
    """Append-only, queryable history: console lines and the audit log.

    Filters (all optional): device, level/action, q (text contains), since, until
    (epoch seconds), before_id (paging, newest first), limit.
    """
    def append_console(self, entries: list[dict]) -> None: ...
    def query_console(self, **filters: Any) -> list[dict]: ...
    def append_audit(self, entry: dict) -> None: ...
    def query_audit(self, **filters: Any) -> list[dict]: ...
    def prune(self, console_before: float, audit_before: float) -> None: ...


class LogSource(Protocol):
    """Read-only access to a tree of log files. Paths are '/'-separated and relative
    to the source root, e.g. "2026-06-09/YAKKALA/YAKKALA-D04-20260609-1816.txt"."""
    def listdir(self, rel: str) -> list[dict]: ...           # [{name, is_dir, size, mtime}]
    def stat(self, rel: str) -> dict: ...                    # {size, mtime}; FileNotFoundError if missing
    def read(self, rel: str, offset: int, length: int) -> bytes: ...
    def close(self) -> None: ...
