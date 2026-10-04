/**
 * Live state fed by the backend WebSocket (/ws). A tiny external store read
 * with useSyncExternalStore — no state library needed.
 */
import { useSyncExternalStore } from "react";
import type { Batch, ConfigValue, ConsoleEntry, DevInfoValue, FleetRow, FleetSnapshot, Job, MqttStatus, OtaLogLine, OtaSession } from "./types";

const MAX_CONSOLE = 3000;

export interface LiveState {
  wsConnected: boolean;
  mqtt: MqttStatus | null;
  console: ConsoleEntry[];
  config: Record<string, Record<string, ConfigValue>>;     // device_id → key → value
  devinfo: Record<string, Record<string, DevInfoValue>>;
  written: Record<string, Record<string, { verified: boolean; ts: number }>>;
  jobs: Record<string, Job>;                                // job_id → job
  ota: Record<string, OtaSession>;                          // device_id → session
  otaLog: Record<string, OtaLogLine[]>;
  fleet: Record<string, FleetRow>;                          // fleetKey(row) → row
  fleetTimeout: number;
  clockSkew: number;                                        // server now − browser now (s)
  batches: Record<string, Batch>;
  serverApiVersion: number | null;
}

export const fleetKey = (r: FleetRow) => r.device_id ?? `t:${r.topic_id}`;

let state: LiveState = {
  wsConnected: false, mqtt: null, console: [], config: {}, devinfo: {}, written: {}, jobs: {}, ota: {}, otaLog: {},
  fleet: {}, fleetTimeout: 180, clockSkew: 0, batches: {}, serverApiVersion: null,
};

/** Merge OTA sessions (with logs) from GET /api/ota. */
export function seedOta(sessions: OtaSession[]) {
  const ota = { ...state.ota }, otaLog = { ...state.otaLog };
  for (const s of sessions) { ota[s.device_id] = s; if (s.log) otaLog[s.device_id] = s.log; }
  set({ ota, otaLog });
}

export function seedFleet(f: FleetSnapshot) {
  const fleet: Record<string, FleetRow> = {};
  f.devices.forEach((r) => (fleet[fleetKey(r)] = r));
  set({ fleet, fleetTimeout: f.online_timeout_s, clockSkew: f.now - Date.now() / 1000 });
}
const listeners = new Set<() => void>();

function set(patch: Partial<LiveState>) {
  state = { ...state, ...patch };
  listeners.forEach((l) => l());
}

export function useLive<T>(select: (s: LiveState) => T): T {
  return useSyncExternalStore((l) => { listeners.add(l); return () => listeners.delete(l); }, () => select(state));
}

/** Merge a device's server-side cached state (GET /devices/{id}/state). */
export function seedDevice(deviceId: string, config: Record<string, ConfigValue>, devinfo: Record<string, DevInfoValue>,
                           jobs: Job[], ota: OtaSession | null) {
  const j = { ...state.jobs };
  jobs.forEach((x) => (j[x.job_id] = x));
  const patch: Partial<LiveState> = {
    config: { ...state.config, [deviceId]: { ...config, ...state.config[deviceId] } },
    devinfo: { ...state.devinfo, [deviceId]: { ...devinfo, ...state.devinfo[deviceId] } },
    jobs: j,
  };
  if (ota) {
    patch.ota = { ...state.ota, [deviceId]: ota };
    if (ota.log) patch.otaLog = { ...state.otaLog, [deviceId]: ota.log };
  }
  set(patch);
}

function nested<V>(map: Record<string, Record<string, V>>, a: string, b: string, v: V) {
  return { ...map, [a]: { ...map[a], [b]: v } };
}

/** Live log text (logs.data) goes straight to subscribers instead of the store. */
type LogListener = (ev: { type: string; text?: string; size?: number; error?: string; path?: string; name?: string }) => void;
const logListeners = new Map<string, LogListener>();
export function onLogData(followId: string, fn: LogListener): () => void {
  logListeners.set(followId, fn);
  return () => { logListeners.delete(followId); };
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function handle(ev: any) {
  switch (ev.type) {
    case "logs.data": case "logs.error": case "logs.tick": case "logs.switch":
      logListeners.get(ev.follow_id)?.(ev); break;
    case "hello": {
      const ota: Record<string, OtaSession> = {};
      const otaLog: Record<string, OtaLogLine[]> = {};
      for (const s of ev.ota as OtaSession[]) { ota[s.device_id] = s; otaLog[s.device_id] = s.log ?? []; }
      const batches: Record<string, Batch> = {};
      for (const b of (ev.batches ?? []) as Batch[]) batches[b.batch_id] = b;
      set({ mqtt: ev.mqtt, console: ev.console, ota, otaLog, batches, serverApiVersion: ev.api_version ?? 0 });
      break;
    }
    case "fleet.update": {
      const fleet = { ...state.fleet };
      for (const r of ev.devices as FleetRow[]) fleet[fleetKey(r)] = r;
      set({ fleet });
      break;
    }
    case "fleet.removed": {
      const fleet = { ...state.fleet };
      delete fleet[`t:${ev.topic_id}`];
      set({ fleet });
      break;
    }
    case "ota.batch": set({ batches: { ...state.batches, [ev.batch.batch_id]: ev.batch } }); break;
    case "mqtt.status": set({ mqtt: ev.status }); break;
    case "console": {
      const c = state.console.length >= MAX_CONSOLE ? state.console.slice(-MAX_CONSOLE + 500) : state.console.slice();
      c.push(ev.entry);
      set({ console: c });
      break;
    }
    case "console.cleared": set({ console: [] }); break;
    case "config.value":
      set({ config: nested(state.config, ev.device_id, String(ev.key),
        { key: ev.key, type_id: ev.type_id, value_type: ev.value_type, value: ev.value, ts: ev.ts }) });
      break;
    case "config.written":
      set({ written: nested(state.written, ev.device_id, String(ev.key), { verified: ev.verified, ts: Date.now() / 1000 }) });
      break;
    case "devinfo.value":
      set({ devinfo: nested(state.devinfo, ev.device_id, String(ev.key), { key: ev.key, valid: ev.valid, value: ev.value, ts: ev.ts }) });
      break;
    case "job.progress":
      set({ jobs: { ...state.jobs, [ev.job_id]: { job_id: ev.job_id, device_id: ev.device_id, kind: ev.kind,
        done: ev.done, total: ev.total, running: true } } });
      break;
    case "job.done": {
      const prev = state.jobs[ev.job_id];
      set({ jobs: { ...state.jobs, [ev.job_id]: { ...(prev ?? { job_id: ev.job_id, device_id: ev.device_id, kind: ev.kind, done: 0, total: 0 }),
        running: false, ok: ev.ok, errors: ev.errors } } });
      break;
    }
    case "ota.state": {
      const patch: Partial<LiveState> = { ota: { ...state.ota, [ev.device_id]: ev.session } };
      // a new session starts with a fresh log
      const prev = state.ota[ev.device_id];
      if (ev.session.state === "running" && (!prev || prev.started !== ev.session.started)) {
        patch.otaLog = { ...state.otaLog, [ev.device_id]: [] };
      }
      set(patch);
      break;
    }
    case "ota.progress": {
      const s = state.ota[ev.device_id];
      if (s) set({ ota: { ...state.ota, [ev.device_id]: { ...s, progress: ev.pct } } });
      break;
    }
    case "ota.log": {
      const log = [...(state.otaLog[ev.device_id] ?? []), { ts: Date.now() / 1000, level: ev.level, text: ev.text }].slice(-400);
      set({ otaLog: { ...state.otaLog, [ev.device_id]: log } });
      break;
    }
  }
}

/** Keep a WebSocket open to /ws, reconnecting with backoff. */
export function startLiveConnection() {
  let delay = 500;
  const open = () => {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    const ws = new WebSocket(`${proto}://${location.host}/ws`);
    ws.onopen = () => { delay = 500; set({ wsConnected: true }); };
    ws.onmessage = (m) => { try { handle(JSON.parse(m.data)); } catch { /* ignore malformed */ } };
    ws.onclose = () => {
      set({ wsConnected: false });
      setTimeout(open, delay);
      delay = Math.min(delay * 2, 10000);
    };
  };
  open();
}

/** Current state outside React (e.g. to await a job). */
export function getLive(): LiveState { return state; }

/** Resolve when a background job finishes (via job.done events). */
export function waitForJob(jobId: string, timeoutMs = 300000): Promise<Job> {
  return new Promise((resolve, reject) => {
    const t0 = Date.now();
    const tick = () => {
      const j = state.jobs[jobId];
      if (j && !j.running) return resolve(j);
      if (Date.now() - t0 > timeoutMs) return reject(new Error("Timed out waiting for the device"));
      setTimeout(tick, 250);
    };
    tick();
  });
}
