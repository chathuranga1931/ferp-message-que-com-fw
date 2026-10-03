"""Pydantic models shared by services, adapters and the API."""

from typing import Literal

from pydantic import BaseModel, Field

PASSWORD_MASK = "********"


# ── App (software) config — persisted as JSON, editable from the Settings page ─

class MqttConfig(BaseModel):
    host:         str  = "localhost"
    port:         int  = Field(1883, ge=1, le=65535)
    username:     str  = ""
    password:     str  = ""
    client_id:    str  = ""            # empty → auto "ferp-web-<random>"
    keepalive:    int  = Field(60, ge=5, le=3600)
    dev_type:     str  = "ferp-com"    # ferp/{dev_type}/{group}/{id}/...
    auto_connect: bool = True          # connect at backend start-up
    brokers:      list[str] = ["localhost", "broker.emqx.io"]  # presets for the host picker


class DeviceOpsConfig(BaseModel):
    response_timeout_s: float = Field(5.0, ge=0.5, le=60)
    verify_writes:      bool  = True   # read each key back after writing it


class OtaConfig(BaseModel):
    chunk_size: int = Field(4096, ge=256, le=65536)
    bundle_dirs: list[str] = []   # folders scanned for .bdl files to import; empty → repo releases/ folder


class ConsoleConfig(BaseModel):
    buffer_lines: int = Field(2000, ge=100, le=50000)


class FleetConfig(BaseModel):
    online_timeout_s:  int = Field(180, ge=10, le=86400)   # "online" = heard from within this window
    probe_timeout_s:   float = Field(3.0, ge=0.5, le=30)
    auto_probe_interval_s: int = Field(0, ge=0, le=86400)  # 0 = off; else ping registered devices periodically


class HistoryConfig(BaseModel):
    persist_console:        bool = True
    console_retention_days: int  = Field(30, ge=1, le=3650)
    audit_retention_days:   int  = Field(365, ge=1, le=36500)


class LogsConfig(BaseModel):
    """Cloud log server (replaces tools/cloud-udp-monitor/runssh)."""
    source:            Literal["ssh", "local"] = "ssh"
    host:              str   = "144.24.156.245"
    port:              int   = Field(22, ge=1, le=65535)
    username:          str   = "ubuntu"
    key_path:          str   = ""      # private key file on this PC (must be readable by the service account)
    key_passphrase:    str   = ""
    root:              str   = "logs"  # remote folder: <root>/<YYYY-MM-DD>/<SHED>/<SHED>-<PUMP>-<YYYYMMDD>-<HHMM>*.txt
    local_root:        str   = ""      # used when source == "local"
    follow_interval_s: float = Field(1.0, ge=0.3, le=30)


class AppConfig(BaseModel):
    mqtt:    MqttConfig      = MqttConfig()
    device:  DeviceOpsConfig = DeviceOpsConfig()
    ota:     OtaConfig       = OtaConfig()
    console: ConsoleConfig   = ConsoleConfig()
    fleet:   FleetConfig     = FleetConfig()
    history: HistoryConfig   = HistoryConfig()
    logs:    LogsConfig      = LogsConfig()

    def masked(self) -> "AppConfig":
        c = self.model_copy(deep=True)
        if c.mqtt.password:
            c.mqtt.password = PASSWORD_MASK
        if c.logs.key_passphrase:
            c.logs.key_passphrase = PASSWORD_MASK
        return c


# ── Device registry ─────────────────────────────────────────────────────────

class DeviceIn(BaseModel):
    label: str = Field(..., min_length=1)
    mac:   str = ""
    uuid:  str = ""
    group: str = "default"   # MQTT topic group
    ip:    str = ""      # kept for compatibility with ferp_devices.json; unused by the web app
    notes: str = ""
    # site / hardware details (registry only; pump IDs and board version can be read from the device)
    shed:          str = ""   # station name, as used in cloud log paths (e.g. YAKKALA)
    pump_id_1:     str = ""   # device config NOZZLE_0_ID (0x6007), max 4 chars on the device
    pump_id_2:     str = ""   # device config NOZZLE_1_ID (0x6008)
    sd_card_size:  str = ""   # e.g. "8 GB" (not yet reported by the firmware over MQTT)
    board_version: str = ""   # device info HW_VERSION (0xA005)
    pump_type:     str = ""


class Device(DeviceIn):
    id: str

    @property
    def mqtt_id(self) -> str:
        """Device id used in topics: provisioned UUID, else MAC."""
        return self.uuid or self.mac


# ── Request bodies ──────────────────────────────────────────────────────────

class SendMessageIn(BaseModel):
    msg:  str
    data: dict = {}


class KeysIn(BaseModel):
    keys: list[int] | None = None    # None → all known keys


class ConfigWriteItem(BaseModel):
    key:   int
    value: str


class ConfigWriteIn(BaseModel):
    values: list[ConfigWriteItem]


class OtaStartIn(BaseModel):
    firmware_id: str
    chunk_size:  int | None = Field(None, ge=256, le=65536)


class ProbeIn(BaseModel):
    device_ids: list[str] | None = None    # None → all registered devices


class SnapshotIn(BaseModel):
    name:      str = Field(..., min_length=1)
    notes:     str = ""
    device_id: str | None = None           # take values from this device's last-read config …
    values:    dict[str, str] | None = None  # … or explicit {"0x1001": "value"} (import)


class SnapshotApplyIn(BaseModel):
    device_ids: list[str] = Field(..., min_length=1)
    keys:       list[int] | None = None    # subset; None → every key in the snapshot
    only_diff:  bool = True                # skip keys whose last-read value already matches


class FavoriteIn(BaseModel):
    name: str = Field(..., min_length=1)
    msg:  str
    data: dict = {}


class FirmwareImportIn(BaseModel):
    paths: list[str] = Field(..., min_length=1)


class FirmwarePatchIn(BaseModel):
    notes: str = ""


class LogFollowIn(BaseModel):
    path: str
    from_end_bytes: int = Field(16384, ge=0, le=5_000_000)   # start this far before the end …
    from_offset: int | None = Field(None, ge=0)              # … or continue exactly from here (no gap)


class BatchOtaIn(BaseModel):
    firmware_id:     str
    device_ids:      list[str] = Field(..., min_length=1)
    chunk_size:      int | None = Field(None, ge=256, le=65536)
    concurrency:     int = Field(1, ge=1, le=5)
    stop_on_failure: bool = True
