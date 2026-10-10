import type {
  AppConfig, AuditEntry, Batch, Catalog, Device, DeviceLogEntry, DeviceLogIn, DeviceSave, DeviceState, Harness, Shed, ShedIn, Favorite, Firmware, FleetSnapshot,
  HistoryLine, LogChunk, LogFile, MqttStatus, OtaSession, RecentMessage, ScannedBundle, Snapshot,
} from "./types";

export class ApiError extends Error {
  constructor(public status: number, message: string) { super(message); }
}

async function req<T>(method: string, path: string, body?: unknown): Promise<T> {
  const init: RequestInit = { method, headers: {} };
  if (body instanceof FormData) init.body = body;
  else if (body !== undefined) {
    init.body = JSON.stringify(body);
    (init.headers as Record<string, string>)["Content-Type"] = "application/json";
  }
  const r = await fetch(`/api${path}`, init);
  const text = await r.text();
  const data = text ? JSON.parse(text) : null;
  if (!r.ok) {
    const detail = data?.detail;
    throw new ApiError(r.status, typeof detail === "string" ? detail : JSON.stringify(detail ?? r.statusText));
  }
  return data as T;
}

export const api = {
  me: () => req<{ user: string; auth_mode: string }>("GET", "/me"),
  getConfig: () => req<AppConfig>("GET", "/config"),
  putConfig: (cfg: AppConfig) => req<{ config: AppConfig; reconnected: boolean }>("PUT", "/config", cfg),

  mqtt: () => req<MqttStatus>("GET", "/mqtt"),
  mqttConnect: () => req<MqttStatus>("POST", "/mqtt/connect"),
  mqttDisconnect: () => req<MqttStatus>("POST", "/mqtt/disconnect"),

  catalog: () => req<Catalog>("GET", "/catalog"),
  reloadCatalog: () => req<Catalog>("POST", "/catalog/reload"),
  clearConsole: () => req("DELETE", "/console"),

  devices: () => req<Device[]>("GET", "/devices"),
  addDevice: (d: DeviceSave) => req<Device>("POST", "/devices", d),
  updateDevice: (id: string, d: DeviceSave) => req<Device>("PUT", `/devices/${id}`, d),

  sheds: () => req<Shed[]>("GET", "/sheds"),
  addShed: (s: ShedIn) => req<Shed>("POST", "/sheds", s),
  updateShed: (id: string, s: ShedIn) => req<Shed & { renamed_devices: number }>("PUT", `/sheds/${id}`, s),
  deleteShed: (id: string) => req("DELETE", `/sheds/${id}`),
  harnesses: () => req<Harness[]>("GET", "/harnesses"),
  saveHarness: (h: Harness) => req<Harness>("PUT", `/harnesses/${encodeURIComponent(h.id)}`, h),
  deleteHarness: (id: string) => req("DELETE", `/harnesses/${encodeURIComponent(id)}`),
  deviceLog: (id: string) => req<DeviceLogEntry[]>("GET", `/devices/${id}/log`),
  addDeviceLog: (id: string, e: DeviceLogIn) => req<DeviceLogEntry>("POST", `/devices/${id}/log`, e),
  updateDeviceLog: (eid: string, e: DeviceLogIn) => req<DeviceLogEntry>("PUT", `/device-log/${eid}`, e),
  deleteDeviceLog: (eid: string) => req("DELETE", `/device-log/${eid}`),
  openIssues: () => req<Record<string, number>>("GET", "/device-log/open-issues"),
  deleteDevice: (id: string) => req("DELETE", `/devices/${id}`),
  deviceState: (id: string) => req<DeviceState>("GET", `/devices/${id}/state`),
  readSiteInfo: (id: string) =>
    req<{ values: Partial<Record<"pump_id_1" | "pump_id_2" | "board_version", string>>; errors: string[] }>("POST", `/devices/${id}/site-info/read`),

  request: (id: string, msg: string, data: Record<string, unknown>, expect: string, timeout = 10) =>
    req<{ msg: string; seq: number; data: Record<string, unknown> }>("POST", `/devices/${id}/request`, { msg, data, expect, timeout }),
  send: (id: string, msg: string, data: Record<string, unknown>) =>
    req<{ seq: number }>("POST", `/devices/${id}/send`, { msg, data }),
  readConfig: (id: string, keys?: number[]) => req<{ job_id: string }>("POST", `/devices/${id}/config/read`, { keys: keys ?? null }),
  writeConfig: (id: string, values: { key: number; value: string }[]) =>
    req<{ job_id: string }>("POST", `/devices/${id}/config/write`, { values }),
  readDevInfo: (id: string, keys?: number[]) => req<{ job_id: string }>("POST", `/devices/${id}/devinfo/read`, { keys: keys ?? null }),
  cancelJob: (jobId: string) => req("POST", `/jobs/${jobId}/cancel`),

  firmware: () => req<Firmware[]>("GET", "/firmware"),
  uploadFirmware: (file: File, notes = "") => {
    const f = new FormData(); f.append("file", file); f.append("notes", notes);
    return req<Firmware>("POST", "/firmware", f);
  },
  scanFirmware: () => req<{ folders: string[]; files: ScannedBundle[] }>("GET", "/firmware/scan"),
  importFirmware: (paths: string[]) => req<{ results: (Partial<Firmware> & { path: string; error?: string })[] }>("POST", "/firmware/import", { paths }),
  setFirmwareNotes: (fid: string, notes: string) => req<Firmware>("PATCH", `/firmware/${fid}`, { notes }),
  firmwareDownloadUrl: (fid: string) => `/api/firmware/${fid}/download`,
  otaSessions: () => req<OtaSession[]>("GET", "/ota"),
  health: () => req<{ ok: boolean; api_version: number; started: number }>("GET", "/health"),
  restartServer: () => req("POST", "/system/restart"),
  deleteFirmware: (fid: string) => req("DELETE", `/firmware/${fid}`),
  startOta: (id: string, firmware_id: string, chunk_size?: number, allow_board_mismatch?: boolean) =>
    req<OtaSession>("POST", `/devices/${id}/ota`, { firmware_id, chunk_size, allow_board_mismatch }),
  abortOta: (id: string) => req("POST", `/devices/${id}/ota/abort`),
  batches: () => req<Batch[]>("GET", "/ota/batches"),
  startBatch: (b: {
    firmware_ids: string[]; device_ids: string[]; chunk_size?: number; concurrency: number; stop_on_failure: boolean;
    step_delay_s?: number; wait_online?: boolean; online_timeout_s?: number; allow_type_mismatch?: boolean;
    allow_board_mismatch?: boolean;
  }) =>
    req<Batch>("POST", "/ota/batches", b),
  cancelBatch: (bid: string) => req("POST", `/ota/batches/${bid}/cancel`),

  fleet: () => req<FleetSnapshot>("GET", "/fleet"),
  probe: (device_ids?: string[]) => req<{ probing: number }>("POST", "/fleet/probe", { device_ids: device_ids ?? null }),
  forget: (topicId: string) => req("DELETE", `/fleet/${topicId}`),

  snapshots: () => req<Snapshot[]>("GET", "/snapshots"),
  createSnapshot: (s: { name: string; notes?: string; device_id?: string; values?: Record<string, string> }) =>
    req<Snapshot>("POST", "/snapshots", s),
  deleteSnapshot: (sid: string) => req("DELETE", `/snapshots/${sid}`),
  applySnapshot: (sid: string, device_ids: string[], only_diff: boolean, keys?: number[]) =>
    req<{ results: { device_id: string; job_id: string | null; keys: number }[] }>("POST", `/snapshots/${sid}/apply`,
      { device_ids, only_diff, keys: keys ?? null }),

  favorites: () => req<Favorite[]>("GET", "/favorites"),
  addFavorite: (name: string, msg: string, data: Record<string, unknown>) => req<Favorite>("POST", "/favorites", { name, msg, data }),
  deleteFavorite: (fid: string) => req("DELETE", `/favorites/${fid}`),
  recentMessages: () => req<RecentMessage[]>("GET", "/messages/recent"),

  consoleHistory: (q: Record<string, string | number | undefined>) => req<HistoryLine[]>("GET", `/history/console${qs(q)}`),
  auditHistory: (q: Record<string, string | number | undefined>) => req<AuditEntry[]>("GET", `/history/audit${qs(q)}`),
  exportUrl: (kind: "console" | "audit", q: Record<string, string | number | undefined>) => `/api/history/${kind}/export${qs(q)}`,

  logsTest: () => req<{ ok: boolean; ms: number; dates: number; latest: string | null; devices: number }>("POST", "/logs/test"),
  logDevices: (fresh = false) => req<{ mac: string; last_date: string; days: number }[]>("GET", `/logs/devices${qs({ fresh: fresh ? "true" : undefined })}`),
  logDeviceFiles: (mac: string, fresh = false) => req<LogFile[]>("GET", `/logs/device-files${qs({ mac, fresh: fresh ? "true" : undefined })}`),
  logDeviceZipUrl: (mac: string, since?: string) => `/api/logs/device-zip${qs({ mac, since })}`,
  logDates: (fresh = false) => req<string[]>("GET", `/logs/dates${qs({ fresh: fresh ? "true" : undefined })}`),
  logSheds: (date: string, fresh = false) => req<string[]>("GET", `/logs/sheds${qs({ date, fresh: fresh ? "true" : undefined })}`),
  logFiles: (date: string, shed: string, fresh = false) =>
    req<LogFile[]>("GET", `/logs/files${qs({ date, shed, fresh: fresh ? "true" : undefined })}`),
  logRead: (path: string, opts: { offset?: number; length?: number; tail?: boolean }) =>
    req<LogChunk>("GET", `/logs/read${qs({ path, offset: opts.offset, length: opts.length, tail: opts.tail ? "true" : undefined })}`),
  logFollow: (path: string, from_offset: number) =>
    req<{ follow_id: string; size: number }>("POST", "/logs/follow", { path, from_offset }),
  logKeepalive: (fid: string) => req("POST", `/logs/follow/${fid}/keepalive`),
  logUnfollow: (fid: string) => req("DELETE", `/logs/follow/${fid}`),
  logDownloadUrl: (path: string) => `/api/logs/download${qs({ path })}`,
  logZipUrl: (date: string, shed: string, pump?: string) => `/api/logs/zip${qs({ date, shed, pump })}`,
};

function qs(q: Record<string, string | number | undefined>): string {
  const p = new URLSearchParams();
  for (const [k, v] of Object.entries(q)) if (v !== undefined && v !== "") p.set(k, String(v));
  const s = p.toString();
  return s ? `?${s}` : "";
}
