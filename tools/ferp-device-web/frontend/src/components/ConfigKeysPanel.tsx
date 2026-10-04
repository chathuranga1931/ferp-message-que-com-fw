import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { useLive } from "../store";
import type { ConfigKeyDef, Device, Snapshot } from "../types";
import { defName } from "../definitions";
import { EMPTY, hex } from "../util";

/** Normalise a value the way the device will store it, so edits compare correctly. */
function normalise(v: string, type: string): string {
  if (type === "BOOL") return ["1", "true", "yes", "on"].includes(v.trim().toLowerCase()) ? "True" : "False";
  if (type === "UINT32") { const n = Number(v.trim() || 0); return Number.isFinite(n) ? String(Math.trunc(n) >>> 0) : v; }
  return v;
}

const UINT32_MAX = 4294967295;

/** Returns an error message, or null when the value is acceptable for the key. */
function validate(k: ConfigKeyDef, v: string): string | null {
  if (k.type === "UINT32") {
    const t = v.trim();
    if (!/^\d+$/.test(t)) return "Whole number (decimal) required";
    const n = Number(t);
    const lo = k.min ?? 0, hi = k.max ?? UINT32_MAX;
    if (n < lo || n > hi) return `Must be between ${lo} and ${hi}`;
  } else if (k.type === "STRING" && k.max_len && v.length > k.max_len) {
    return `At most ${k.max_len} characters`;
  }
  return null;
}

export function ConfigKeysPanel({ device, keys }: { device: Device; keys: ConfigKeyDef[] }) {
  const values = useLive((s) => s.config[device.id]) ?? EMPTY;
  const written = useLive((s) => s.written[device.id]) ?? EMPTY;
  const connected = useLive((s) => s.mqtt?.connected ?? false);
  const job = useLive((s) => Object.values(s.jobs).filter((j) => j.device_id === device.id && j.kind.startsWith("config")).pop());
  const [edits, setEdits] = useState<Record<string, string>>({});
  const [filter, setFilter] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const [snapshots, setSnapshots] = useState<Snapshot[]>([]);
  const [cmpId, setCmpId] = useState("");
  const [snapName, setSnapName] = useState<string | null>(null);
  const [info, setInfo] = useState<string | null>(null);

  useEffect(() => { setEdits({}); setErr(null); setInfo(null); }, [device.id]);
  const loadSnapshots = () => api.snapshots().then(setSnapshots).catch(() => undefined);
  useEffect(() => { loadSnapshots(); }, []);
  const cmp = snapshots.find((x) => x.id === cmpId);
  const cmpValue = (keyId: number) => cmp?.values[hex(keyId)]?.value;

  // Drop an edit once the device reports that value (verified write or a read showing it).
  useEffect(() => {
    setEdits((prev) => {
      const next = { ...prev };
      let changed = false;
      for (const [k, v] of Object.entries(prev)) {
        const kd = keys.find((x) => String(x.key) === k);
        const cur = values[k];
        if (kd && cur && normalise(v, kd.type) === cur.value) { delete next[k]; changed = true; }
      }
      return changed ? next : prev;
    });
  }, [values, keys]);

  const rows = useMemo(() => {
    const f = filter.trim().toLowerCase();
    return keys.filter((k) => !f || k.name.toLowerCase().includes(f) || k.group.toLowerCase().includes(f) || hex(k.key).toLowerCase().includes(f));
  }, [keys, filter]);

  const changedAll = Object.entries(edits).filter(([k, v]) => {
    const kd = keys.find((x) => String(x.key) === k);
    return kd && normalise(v, kd.type) !== values[k]?.value;
  });
  const invalid = changedAll.filter(([k, v]) => validate(keys.find((x) => String(x.key) === k)!, v));
  const changed = changedAll.filter(([k]) => !invalid.some(([ik]) => ik === k));
  const running = job?.running ?? false;

  const run = (p: Promise<unknown>) => { setErr(null); p.catch((e) => setErr(e.message)); };
  const writeKeys = (list: [string, string][]) =>
    run(api.writeConfig(device.id, list.map(([k, v]) => ({ key: Number(k), value: v }))));

  const saveSnapshot = async () => {
    if (!snapName?.trim()) return;
    setErr(null);
    try {
      const snap = await api.createSnapshot({ name: snapName.trim(), device_id: device.id });
      setSnapName(null);
      setInfo(`Snapshot "${snap.name}" saved (${Object.keys(snap.values).length} keys)`);
      loadSnapshots();
    } catch (e) { setErr((e as Error).message); }
  };

  /** Put the snapshot's differing values into the editor for review before writing. */
  const loadSnapshotIntoEditor = () => {
    if (!cmp) return;
    const next = { ...edits };
    let n = 0;
    for (const k of keys) {
      const sv = cmpValue(k.key);
      if (sv !== undefined && sv !== values[String(k.key)]?.value) { next[String(k.key)] = sv; n++; }
    }
    setEdits(next);
    setInfo(n ? `${n} value(s) from "${cmp.name}" loaded — review, then Write changes` : "Device already matches the snapshot");
  };

  const groups = useMemo(() => {
    const out: { group: string; rows: ConfigKeyDef[] }[] = [];
    for (const k of rows) {
      const last = out[out.length - 1];
      if (last && last.group === k.group) last.rows.push(k); else out.push({ group: k.group, rows: [k] });
    }
    return out;
  }, [rows]);
  const loadedCount = Object.keys(values).length;

  return (
    <section className="card">
      <div className="card-head">
        <h3>Config keys</h3>
        <div className="row wrap">
          <button className="btn small" disabled={!connected || running} onClick={() => run(api.readConfig(device.id))}>↓ Read all</button>
          <button className="btn small primary" disabled={!connected || running || changed.length === 0}
                  onClick={() => writeKeys(changed)}>↑ Write changes{changed.length ? ` (${changed.length})` : ""}</button>
          {changedAll.length > 0 && <button className="btn small ghost" onClick={() => setEdits({})}>Discard</button>}
          {running && job && <button className="btn small ghost" onClick={() => api.cancelJob(job.job_id)}>Cancel</button>}
        </div>
      </div>
      <div className="row wrap snap-bar">
        <select value={cmpId} onChange={(e) => setCmpId(e.target.value)} title="Show a snapshot's values next to the device's">
          <option value="">Compare with snapshot…</option>
          {snapshots.map((x) => <option key={x.id} value={x.id}>{x.name}</option>)}
        </select>
        {cmp && <button className="btn small" onClick={loadSnapshotIntoEditor}>Load into editor</button>}
        {snapName === null ? (
          <button className="btn small ghost" disabled={loadedCount === 0} title={loadedCount ? "" : "Read the config first"}
                  onClick={() => setSnapName(`${device.label} ${new Date().toISOString().slice(0, 10)}`)}>Save as snapshot</button>
        ) : (
          <>
            <input value={snapName} onChange={(e) => setSnapName(e.target.value)} onKeyDown={(e) => e.key === "Enter" && saveSnapshot()} autoFocus />
            <button className="btn small primary" onClick={saveSnapshot}>Save</button>
            <button className="btn small ghost" onClick={() => setSnapName(null)}>Cancel</button>
          </>
        )}
        {invalid.length > 0 && <span className="inline-error">{invalid.length} invalid value(s) will not be written</span>}
        {info && <span className="ok-text">{info}</span>}
      </div>
      {job && (running || job.ok === false) && (
        <div className="job-line">
          <progress max={job.total || 1} value={job.done} />
          <span className="muted">{job.kind === "config.read" ? "Reading" : "Writing"} {job.done}/{job.total}
            {!running && job.errors?.length ? ` — ${job.errors[0]}` : ""}</span>
        </div>
      )}
      {err && <div className="inline-error">{err}</div>}
      <input className="search" placeholder="Filter keys…" value={filter} onChange={(e) => setFilter(e.target.value)} />
      <div className="table-wrap">
        <table className="keys">
          <thead><tr><th>Key</th><th>Name</th><th>Type</th><th className="w-value">Value</th>{cmp && <th>Snapshot</th>}<th /></tr></thead>
          <tbody>
            {groups.map((g) => [
              <tr key={`g-${g.group}`} className="group-row"><td colSpan={cmp ? 6 : 5}>{g.group}</td></tr>,
              ...g.rows.map((k) => {
              const id = String(k.key);
              const cur = values[id];
              const edit = edits[id];
              const isMod = edit !== undefined && normalise(edit, k.type) !== cur?.value;
              const bad = isMod ? validate(k, edit) : null;
              const w = written[id];
              const state = bad ? "invalid" : isMod ? "modified" : cur ? "loaded" : "unloaded";
              const sv = cmp ? cmpValue(k.key) : undefined;
              const shown = edit ?? cur?.value ?? "";
              const setV = (v: string) => setEdits({ ...edits, [id]: v });
              return (
                <tr key={id} className={state}>
                  <td><code>{hex(k.key)}</code></td>
                  <td>{k.name}{k.description && <div className="muted small desc">{k.description}</div>}</td>
                  <td className="muted">{k.type}</td>
                  <td>
                    {k.type === "BOOL" ? (
                      <select value={shown === "" ? "" : normalise(shown, "BOOL")} onChange={(e) => setV(e.target.value)}>
                        {!cur && edit === undefined && <option value="">—</option>}
                        <option value="True">True</option><option value="False">False</option>
                      </select>
                    ) : (
                      <input value={shown} placeholder={cur ? "" : "not read"} inputMode={k.type === "UINT32" ? "numeric" : undefined}
                             maxLength={k.max_len ?? undefined} title={bad ?? undefined} aria-invalid={!!bad}
                             onChange={(e) => setV(e.target.value)} />
                    )}
                    {bad && <div className="inline-error small">{bad}</div>}
                    {k.name === "DISPLAY_TYPE" && shown !== "" && (
                      <div className="muted small">{defName("display-type", shown.trim()) ?? "unknown display type"}</div>
                    )}
                  </td>
                  {cmp && <td className={`mono ${sv !== undefined && sv !== cur?.value ? "diff-cell" : "muted"}`}>{sv ?? "—"}</td>}
                  <td className="row-actions">
                    <button className="icon-btn" disabled={!connected || running} title="Read from device"
                            onClick={() => run(api.readConfig(device.id, [k.key]))}>↻</button>
                    <button className="icon-btn" disabled={!connected || running || edit === undefined || !!bad} title="Write this key"
                            onClick={() => writeKeys([[id, edit ?? ""]])}>✎</button>
                    {w && !isMod && <span className={w.verified ? "ok-text" : "inline-error"} title={w.verified ? "Write verified by read-back" : "Read-back did not match"}>{w.verified ? "✓" : "✗"}</span>}
                  </td>
                </tr>
              );
              }),
            ])}
          </tbody>
        </table>
      </div>
    </section>
  );
}
