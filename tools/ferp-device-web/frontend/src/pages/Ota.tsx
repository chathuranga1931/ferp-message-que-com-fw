import { useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import { seedFleet, seedOta, useLive } from "../store";
import type { Batch, Device, Firmware, ScannedBundle } from "../types";
import { fmtAgo, fmtBytes, fmtDateTime, fmtTime, topicId, useNow } from "../util";

interface Props { devices: Device[]; preselect: string[]; onPreselectUsed: () => void }

export default function Ota({ devices, preselect, onPreselectUsed }: Props) {
  const [library, setLibrary] = useState<Firmware[]>([]);
  const refresh = () => api.firmware().then(setLibrary).catch(() => undefined);
  useEffect(() => {
    refresh();
    api.fleet().then(seedFleet).catch(() => undefined);
    api.otaSessions().then(seedOta).catch(() => undefined);
  }, []);

  return (
    <div className="stack">
      <FirmwareLibrary library={library} onChanged={refresh} />
      <FlashDevices library={library} devices={devices} preselect={preselect} onPreselectUsed={onPreselectUsed} />
      <OtaActivity devices={devices} />
    </div>
  );
}

// ── library ─────────────────────────────────────────────────────────────────

function FirmwareLibrary({ library, onChanged }: { library: Firmware[]; onChanged: () => void }) {
  const [busy, setBusy] = useState(false);
  const [msgs, setMsgs] = useState<{ ok: boolean; text: string }[]>([]);
  const [drag, setDrag] = useState(false);
  const [scanOpen, setScanOpen] = useState(false);
  const [confirmDel, setConfirmDel] = useState<string | null>(null);
  const [editNotes, setEditNotes] = useState<{ id: string; text: string } | null>(null);
  const [filter, setFilter] = useState("");
  const fileRef = useRef<HTMLInputElement>(null);

  const upload = async (files: FileList | File[]) => {
    setBusy(true);
    const out: { ok: boolean; text: string }[] = [];
    for (const f of Array.from(files)) {
      if (!f.name.toLowerCase().endsWith(".bdl")) { out.push({ ok: false, text: `${f.name}: not a .bdl file` }); continue; }
      try {
        const m = await api.uploadFirmware(f);
        out.push({ ok: true, text: m.duplicate ? `${f.name}: already in the library` : `${f.name}: added (${m.name} v${m.version})` });
      } catch (e) { out.push({ ok: false, text: `${f.name}: ${(e as Error).message}` }); }
    }
    setMsgs(out); setBusy(false); onChanged();
    if (fileRef.current) fileRef.current.value = "";
  };

  const del = async (id: string) => {
    if (confirmDel !== id) { setConfirmDel(id); return; }
    setConfirmDel(null);
    await api.deleteFirmware(id).catch((e) => setMsgs([{ ok: false, text: e.message }]));
    onChanged();
  };

  const saveNotes = async () => {
    if (!editNotes) return;
    await api.setFirmwareNotes(editNotes.id, editNotes.text).catch((e) => setMsgs([{ ok: false, text: e.message }]));
    setEditNotes(null); onChanged();
  };

  const f = filter.trim().toLowerCase();
  const rows = library.filter((b) => !f || [b.name, b.version, b.filename, b.notes ?? ""].some((x) => x.toLowerCase().includes(f)));

  return (
    <section className={`card ${drag ? "dropping" : ""}`}
             onDragOver={(e) => { e.preventDefault(); setDrag(true); }} onDragLeave={() => setDrag(false)}
             onDrop={(e) => { e.preventDefault(); setDrag(false); if (e.dataTransfer.files.length) upload(e.dataTransfer.files); }}>
      <div className="card-head">
        <h3>Firmware library <span className="muted">({library.length})</span></h3>
        <div className="row wrap">
          <input className="search small" placeholder="Filter…" value={filter} onChange={(e) => setFilter(e.target.value)} />
          <input ref={fileRef} type="file" accept=".bdl" multiple hidden onChange={(e) => e.target.files && upload(e.target.files)} />
          <button className="btn small primary" disabled={busy} onClick={() => fileRef.current?.click()}>{busy ? "Uploading…" : "Upload .bdl"}</button>
          <button className={`btn small ${scanOpen ? "" : "ghost"}`} onClick={() => setScanOpen(!scanOpen)}>Import from folder</button>
        </div>
      </div>
      <p className="muted small">Drop .bdl files anywhere on this card. Scripts can upload too:
        <code> curl -F file=@bundle.bdl -F notes="…" http://&lt;host&gt;:8700/api/firmware</code></p>
      {msgs.map((m, i) => <div key={i} className={m.ok ? "ok-text" : "inline-error"}>{m.text}</div>)}
      {scanOpen && <FolderImport onImported={(text) => { setMsgs([{ ok: true, text }]); onChanged(); }} />}
      <div className="table-wrap">
        <table className="list">
          <thead><tr><th>Target</th><th>Version</th><th>File</th><th>Built</th><th>Size</th><th>Added</th><th>Notes</th><th /></tr></thead>
          <tbody>
            {rows.map((b) => (
              <tr key={b.id}>
                <td><b>{b.name}</b></td>
                <td><code>{b.version}</code></td>
                <td className="muted">{b.filename}</td>
                <td>{fmtDateTime(b.built)}</td>
                <td>{fmtBytes(b.size)}</td>
                <td className="muted small">{fmtDateTime(b.uploaded)}{b.uploaded_by ? ` · ${b.uploaded_by}` : ""}
                  {b.source && b.source !== "upload" && <div title={b.source}>from folder</div>}</td>
                <td className="wrap-cell">
                  {editNotes?.id === b.id ? (
                    <span className="row">
                      <input autoFocus value={editNotes.text} onChange={(e) => setEditNotes({ id: b.id, text: e.target.value })}
                             onKeyDown={(e) => { if (e.key === "Enter") saveNotes(); if (e.key === "Escape") setEditNotes(null); }} />
                      <button className="btn small" onClick={saveNotes}>Save</button>
                    </span>
                  ) : (
                    <button className="link-btn notes" onClick={() => setEditNotes({ id: b.id, text: b.notes ?? "" })} title="Edit notes">
                      {b.notes || <span className="muted">add note</span>}
                    </button>
                  )}
                </td>
                <td className="row-actions">
                  <a className="btn small" href={api.firmwareDownloadUrl(b.id)} download>Download</a>
                  <button className={`btn small ${confirmDel === b.id ? "danger" : "ghost"}`} onClick={() => del(b.id)}
                          onBlur={() => setConfirmDel(null)}>{confirmDel === b.id ? "Confirm" : "Delete"}</button>
                </td>
              </tr>
            ))}
            {rows.length === 0 && <tr><td colSpan={8} className="muted center">No bundles yet — upload a .bdl or import from a folder.</td></tr>}
          </tbody>
        </table>
      </div>
    </section>
  );
}

function FolderImport({ onImported }: { onImported: (text: string) => void }) {
  const [scan, setScan] = useState<{ folders: string[]; files: ScannedBundle[] } | null>(null);
  const [sel, setSel] = useState<Set<string>>(new Set());
  const [err, setErr] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const load = () => api.scanFirmware().then((s) => { setScan(s); setSel(new Set()); }).catch((e) => setErr(e.message));
  useEffect(() => { load(); }, []);

  const doImport = async () => {
    setBusy(true); setErr(null);
    try {
      const r = await api.importFirmware([...sel]);
      const ok = r.results.filter((x) => !x.error).length;
      const bad = r.results.filter((x) => x.error);
      if (bad.length) setErr(bad.map((x) => `${x.path}: ${x.error}`).join("; "));
      onImported(`Imported ${ok} bundle(s)`);
      await load();
    } catch (e) { setErr((e as Error).message); }
    finally { setBusy(false); }
  };

  const importable = scan?.files.filter((f) => !f.in_library && !f.error) ?? [];
  return (
    <div className="edit-box">
      <div className="row wrap">
        <b>Bundles found in</b> <code>{scan?.folders.join(", ") ?? "…"}</code>
        <button className="btn small ghost" onClick={load}>Rescan</button>
        <span className="muted small">(change the folders in Settings → OTA)</span>
      </div>
      {err && <div className="inline-error">{err}</div>}
      {scan && (
        <div className="table-wrap scan-table">
          <table className="list">
            <thead><tr>
              <th><input type="checkbox" aria-label="Select all" checked={importable.length > 0 && importable.every((f) => sel.has(f.path))}
                         onChange={(e) => setSel(e.target.checked ? new Set(importable.map((f) => f.path)) : new Set())} /></th>
              <th>File</th><th>Target</th><th>Version</th><th>Built</th><th>Status</th>
            </tr></thead>
            <tbody>
              {scan.files.map((f) => (
                <tr key={f.path}>
                  <td><input type="checkbox" disabled={f.in_library || !!f.error} checked={sel.has(f.path)} aria-label={`Select ${f.filename}`}
                             onChange={() => { const n = new Set(sel); if (n.has(f.path)) n.delete(f.path); else n.add(f.path); setSel(n); }} /></td>
                  <td title={f.path}>{f.relpath}</td>
                  <td>{f.name ?? "—"}</td>
                  <td><code>{f.version ?? "—"}</code></td>
                  <td>{f.built ? fmtDateTime(f.built) : "—"}</td>
                  <td>{f.error ? <span className="inline-error">{f.error}</span> : f.in_library ? <span className="ok-text">in library</span> : <span className="muted">new</span>}</td>
                </tr>
              ))}
              {scan.files.length === 0 && <tr><td colSpan={6} className="muted center">No .bdl files in these folders.</td></tr>}
            </tbody>
          </table>
        </div>
      )}
      <div className="row">
        <button className="btn primary" disabled={sel.size === 0 || busy} onClick={doImport}>Import selected ({sel.size})</button>
      </div>
    </div>
  );
}

// ── flashing ────────────────────────────────────────────────────────────────

function FlashDevices({ library, devices, preselect, onPreselectUsed }:
  { library: Firmware[]; devices: Device[]; preselect: string[]; onPreselectUsed: () => void }) {
  const fleet = useLive((s) => s.fleet);
  const timeout = useLive((s) => s.fleetTimeout);
  const skew = useLive((s) => s.clockSkew);
  const ota = useLive((s) => s.ota);
  const now = useNow(5000) + skew;
  const [fwId, setFwId] = useState("");
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [chunk, setChunk] = useState(4096);
  const [concurrency, setConcurrency] = useState(1);
  const [stopOnFailure, setStopOnFailure] = useState(true);
  const [confirm, setConfirm] = useState(false);
  const [filter, setFilter] = useState("");
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => { api.getConfig().then((c) => setChunk(c.ota.chunk_size)).catch(() => undefined); }, []);
  useEffect(() => { setFwId((cur) => (library.some((b) => b.id === cur) ? cur : library[0]?.id ?? "")); }, [library]);
  useEffect(() => {
    if (preselect.length) { setSelected(new Set(preselect)); onPreselectUsed(); }
  }, [preselect, onPreselectUsed]);
  useEffect(() => setConfirm(false), [fwId, selected.size]);

  const fw = library.find((b) => b.id === fwId);
  const rowFor = (d: Device) => fleet[d.id];
  const f = filter.trim().toLowerCase();
  const rows = useMemo(() => devices.filter((d) => !f || [d.label, d.group, d.mac, d.uuid].some((x) => x.toLowerCase().includes(f))),
    [devices, f]);
  const allSel = rows.length > 0 && rows.every((d) => selected.has(d.id));

  const start = async () => {
    if (!confirm) { setConfirm(true); return; }
    setConfirm(false); setMsg(null);
    try {
      await api.startBatch({ firmware_id: fwId, device_ids: [...selected], chunk_size: chunk, concurrency, stop_on_failure: stopOnFailure });
      setMsg({ ok: true, text: `Started — progress below` });
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  return (
    <section className="card">
      <div className="card-head"><h3>Flash devices</h3></div>
      <div className="form-grid">
        <label className="field"><span>Bundle</span>
          <select value={fwId} onChange={(e) => setFwId(e.target.value)}>
            {library.length === 0 && <option value="">— library is empty —</option>}
            {library.map((b) => <option key={b.id} value={b.id}>{b.name} v{b.version} — {b.filename}</option>)}
          </select></label>
        <label className="field"><span>Chunk size</span>
          <select value={chunk} onChange={(e) => setChunk(Number(e.target.value))}>
            {[1024, 2048, 4096, 8192].map((c) => <option key={c} value={c}>{c}</option>)}
          </select></label>
        <label className="field"><span>Devices at a time</span>
          <select value={concurrency} onChange={(e) => setConcurrency(Number(e.target.value))}>
            {[1, 2, 3, 4, 5].map((n) => <option key={n} value={n}>{n}</option>)}
          </select></label>
        <label className="check field-check">
          <input type="checkbox" checked={stopOnFailure} onChange={(e) => setStopOnFailure(e.target.checked)} />
          Stop starting new devices after a failure
        </label>
      </div>
      <input className="search" placeholder="Filter devices…" value={filter} onChange={(e) => setFilter(e.target.value)} />
      <div className="table-wrap">
        <table className="list">
          <thead><tr>
            <th><input type="checkbox" checked={allSel} aria-label="Select all"
                       onChange={() => setSelected(allSel ? new Set() : new Set(rows.map((d) => d.id)))} /></th>
            <th>Device</th><th>Group</th><th>Status</th><th>FW now</th><th>OTA</th>
          </tr></thead>
          <tbody>
            {rows.map((d) => {
              const fr = rowFor(d);
              const online = fr?.last_seen ? now - fr.last_seen <= timeout : false;
              const s = ota[d.id];
              return (
                <tr key={d.id} className={selected.has(d.id) ? "selected" : ""}>
                  <td><input type="checkbox" checked={selected.has(d.id)} aria-label={`Select ${d.label}`}
                             onChange={() => { const n = new Set(selected); if (n.has(d.id)) n.delete(d.id); else n.add(d.id); setSelected(n); }} /></td>
                  <td>{d.label}<div className="muted small"><code>{topicId(d.uuid || d.mac)}</code></div></td>
                  <td>{d.group}</td>
                  <td><span className={`dot ${online ? "connected" : fr?.last_seen ? "disconnected" : ""}`} /> {fr?.last_seen ? fmtAgo(fr.last_seen, now) : "never seen"}</td>
                  <td><code>{fr?.info.fw_version ?? "—"}</code></td>
                  <td>{s ? <span className={s.state === "succeeded" ? "ok-text" : s.state === "running" ? "" : "inline-error"}>
                    {s.state}{s.state === "running" ? ` ${s.progress}%` : ""}</span> : <span className="muted">—</span>}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
      <div className="row wrap flash-bar">
        <button className={`btn ${confirm ? "danger" : "primary"}`} disabled={!fw || selected.size === 0} onClick={start}>
          {confirm ? `Confirm: flash ${fw?.name} v${fw?.version} to ${selected.size} device(s)` : `Start OTA (${selected.size})`}
        </button>
        {confirm && <button className="btn ghost" onClick={() => setConfirm(false)}>Cancel</button>}
        {msg && <span className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</span>}
      </div>
    </section>
  );
}

// ── activity ────────────────────────────────────────────────────────────────

const STATE_CLASS: Record<string, string> = {
  succeeded: "ok-text", failed: "inline-error", aborted: "inline-error", cancelled: "inline-error", running: "", pending: "muted", skipped: "muted",
};

function OtaActivity({ devices }: { devices: Device[] }) {
  const batches = useLive((s) => s.batches);
  const ota = useLive((s) => s.ota);
  const otaLog = useLive((s) => s.otaLog);
  const [logDevice, setLogDevice] = useState("");
  const logRef = useRef<HTMLDivElement>(null);
  const list: Batch[] = Object.values(batches).sort((a, b) => b.started - a.started);
  const sessions = Object.values(ota).sort((a, b) => b.started - a.started);
  const log = logDevice ? otaLog[logDevice] ?? [] : [];
  const label = (id: string) => devices.find((d) => d.id === id)?.label ?? id;

  useEffect(() => { if (!logDevice && sessions[0]) setLogDevice(sessions[0].device_id); }, [sessions, logDevice]);
  useEffect(() => { logRef.current?.scrollTo(0, logRef.current.scrollHeight); }, [log]);

  return (
    <section className="card">
      <div className="card-head"><h3>Activity</h3></div>
      {list.length === 0 && sessions.length === 0 && <p className="muted">No OTA has run since the server started.</p>}
      {list.map((b) => {
        const done = b.items.filter((i) => !["pending", "running"].includes(i.state)).length;
        return (
          <div key={b.batch_id} className="batch">
            <div className="row wrap">
              <b>{b.target} v{b.version}</b>
              <span className={STATE_CLASS[b.state]}>{b.state}</span>
              <span className="muted small">{done}/{b.items.length} done · {b.concurrency} at a time · {fmtDateTime(b.started)} by {b.started_by}</span>
              {b.state === "running" && <button className="btn small danger" onClick={() => api.cancelBatch(b.batch_id)}>Cancel</button>}
            </div>
            <div className="batch-items">
              {b.items.map((i) => {
                const s = ota[i.device_id];
                const pct = i.state === "running" ? s?.progress ?? 0 : i.state === "succeeded" ? 100 : 0;
                return (
                  <button key={i.device_id} className={`batch-item ${logDevice === i.device_id ? "active" : ""}`}
                          onClick={() => setLogDevice(i.device_id)} title="Show log">
                    <span className="batch-label">{i.label}</span>
                    <progress max={100} value={pct} />
                    <span className={`small ${STATE_CLASS[i.state]}`} title={i.error}>{i.state}{i.state === "running" ? ` ${pct}%` : ""}</span>
                  </button>
                );
              })}
            </div>
          </div>
        );
      })}
      {sessions.length > 0 && (
        <div className="ota-log-wrap">
          <div className="row wrap">
            <label className="field inline"><span>Log</span>
              <select value={logDevice} onChange={(e) => setLogDevice(e.target.value)}>
                {sessions.map((s) => <option key={s.device_id} value={s.device_id}>{label(s.device_id)} — {s.target} v{s.version} ({s.state})</option>)}
              </select>
            </label>
            {ota[logDevice]?.state === "running" && <button className="btn small danger" onClick={() => api.abortOta(logDevice)}>Abort this device</button>}
          </div>
          <div className="ota-log" ref={logRef}>
            {log.map((l, i) => <div key={i} className={`lvl-${l.level}`}><span className="muted">{fmtTime(l.ts)}</span> {l.text}</div>)}
            {log.length === 0 && <span className="muted">No log lines.</span>}
          </div>
        </div>
      )}
    </section>
  );
}
