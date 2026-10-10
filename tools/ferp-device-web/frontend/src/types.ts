export interface MqttStatus {
  state: "disconnected" | "connecting" | "connected" | "reconnecting";
  connected: boolean;
  host: string;
  port: number;
  dev_type: string;
  error: string | null;
  since: number;
}

export interface AppConfig {
  mqtt: {
    host: string; port: number; username: string; password: string; client_id: string;
    keepalive: number; dev_type: string; auto_connect: boolean; brokers: string[];
  };
  device: { response_timeout_s: number; verify_writes: boolean };
  ota: { chunk_size: number; bundle_dirs: string[]; type_targets: Record<string, string[]>; board_targets: Record<string, string[]> };
  console: { buffer_lines: number };
  fleet: { online_timeout_s: number; probe_timeout_s: number; auto_probe_interval_s: number };
  history: { persist_console: boolean; console_retention_days: number; audit_retention_days: number };
  logs: {
    source: "ssh" | "local"; host: string; port: number; username: string; key_path: string; key_passphrase: string;
    root: string; mac_root: string; mac_scan_days: number; local_root: string; follow_interval_s: number;
  };
}

export interface LogFile { name: string; path: string; size: number; mtime: number; pump: string | null; time: string | null; date?: string | null }
export interface LogChunk { path: string; size: number; offset: number; end: number; text: string }

export interface DeviceIn {
  label: string; mac: string; uuid: string; group: string; ip: string; notes: string;
  shed: string; pump_id_1: string; pump_id_2: string; sd_card_size: string; board_version: string; pump_type: string;
  device_type: string;
}
/** Lifecycle fields (Device page). Kept apart from DeviceIn so the registry form's
 *  string fields stay simple; the server always returns them. */
export interface DeviceLifecycle {
  manufactured: string;      // YYYY-MM-DD
  delivered: string;         // YYYY-MM-DD
  warranty_months: number;
  harnesses: string[];       // SWH0xx
  config_snapshot: string;   // snapshot id last applied
}
export interface Device extends DeviceIn, DeviceLifecycle { id: string }
export type DeviceSave = DeviceIn & Partial<DeviceLifecycle>;

export interface RemoteAccess { tool: string; address: string; notes: string }
export interface ShedIn {
  name: string; customer: string; contact_name: string; phone: string; phone_2: string; email: string;
  address: string; city: string; map_url: string; remote_access: RemoteAccess[]; notes: string;
}
export interface Shed extends ShedIn { id: string }

export interface Harness { id: string; type: string; description: string; retired: boolean; stock: number | null; in_use?: number }

export type LogKind = "manufactured" | "delivered" | "repair" | "modification" | "issue" | "config" | "note";
export interface DeviceLogIn { date: string; kind: LogKind; title: string; details: string; status: "" | "open" | "resolved"; snapshot_id: string }
export interface DeviceLogEntry extends DeviceLogIn { id: string; device_id: string; created: number; user: string }

export interface FieldOption { label: string | number; value: string | number; bar_label?: string; field?: string }
export interface FieldDef {
  name: string; label?: string; type?: string; default?: unknown;
  placeholder?: string; options?: FieldOption[]; min?: number; max?: number;
}
export interface MessageDef { name: string; msg_id: number; direction: "cmd" | "resp" | "any"; fields: FieldDef[]; group: string }
export interface ConfigKeyDef {
  key: number; name: string; type_id: number; type: string; group: string; label: string; description: string;
  min?: number | null; max?: number | null; max_len?: number | null;
  device_types?: string[];   // empty / absent = every device type
}
export interface DevInfoKeyDef { key: number; label: string; field: string }
export interface Catalog {
  messages_dir: string | null;
  groups: { group: string; messages: MessageDef[] }[];
  config_keys: ConfigKeyDef[];
  devinfo_keys: DevInfoKeyDef[];
}

export interface ConfigValue { key: number; type_id: number; value_type: string; value: string; ts: number }
export interface DevInfoValue { key: number; valid: boolean; value: string; ts: number }

export interface ConsoleEntry {
  id: number; ts: number; level: "cmd" | "resp" | "evt" | "info" | "warn" | "error" | "ota";
  text: string; device: string | null; topic: string | null;
}

export interface Job { job_id: string; device_id: string; kind: string; done: number; total: number; running: boolean; ok?: boolean; errors?: string[] }

export interface Firmware {
  id: string; filename: string; name: string; version: string; built: number; size: number; uploaded: number;
  uploaded_by?: string; source?: string; notes?: string; duplicate?: boolean;
  active: boolean;   // true = active list (offered for OTA), false = archive
}
export interface ScannedBundle {
  path: string; folder: string; relpath: string; filename: string; size: number; modified: number;
  name?: string; version?: string; built?: number; id?: string; in_library: boolean; error?: string;
}

export interface OtaLogLine { ts: number; level: string; text: string }
export interface OtaSession {
  device_id: string; device_label: string; firmware_id: string; target: string; version: string;
  chunk_size: number; state: "running" | "succeeded" | "failed" | "aborted";
  progress: number; started: number; finished: number | null; log?: OtaLogLine[];
}

export interface DeviceState {
  config: Record<string, ConfigValue>;
  devinfo: Record<string, DevInfoValue>;
  jobs: Job[];
  ota: OtaSession | null;
  topic_base: string | null;
}

export interface FleetRow {
  device_id: string | null; label: string | null; registered: boolean; topic_id: string; group: string;
  first_seen: number | null; last_seen: number | null; last_msg: string | null; last_kind: string | null;
  msg_count: number; info: Record<string, string>;
  probe: { ts: number; ok: boolean; rtt_ms?: number; error?: string } | null;
  shed?: string; pump_id_1?: string; pump_id_2?: string; pump_type?: string; board_version?: string; sd_card_size?: string;
  device_type?: string;
}
export interface FleetSnapshot { online_timeout_s: number; now: number; devices: FleetRow[] }

export interface BatchStepState { state: "pending" | "waiting" | "running" | "succeeded" | "failed" | "aborted" | "skipped"; error?: string }
export interface BatchItem {
  device_id: string; label: string; state: "pending" | "running" | "succeeded" | "failed" | "aborted" | "skipped" | "cancelled";
  error?: string; step?: number; steps?: BatchStepState[];
}
export interface BatchStep { firmware_id: string; target: string; version: string; filename: string }
export interface Batch {
  batch_id: string; firmware_id: string; target: string; version: string; concurrency: number;
  stop_on_failure: boolean; state: "running" | "succeeded" | "failed" | "cancelled";
  started: number; finished: number | null; started_by: string; items: BatchItem[];
  steps?: BatchStep[]; title?: string; step_delay_s?: number; wait_online?: boolean;
}

export interface SnapshotValue { name: string; type: string; value: string }
export interface Snapshot {
  id: string; name: string; notes: string; created: number; created_by: string;
  source: { device_id: string; label: string } | null; values: Record<string, SnapshotValue>;
}

export interface Favorite { id: string; name: string; msg: string; data: Record<string, unknown>; created: number }
export interface RecentMessage { msg: string; data: Record<string, unknown>; ts: number; device_label: string | null }

export interface HistoryLine { id: number; ts: number; level: string; device: string | null; topic: string | null; text: string }
export interface AuditEntry {
  id: number; ts: number; user: string; action: string; device_id: string | null; device_label: string | null;
  detail: Record<string, unknown>;
}
