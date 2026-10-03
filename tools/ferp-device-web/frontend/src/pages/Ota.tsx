import { useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import { BundlePicker, compareVersions, dtKind } from "../components/BundlePicker";
import { DeviceFilters, matchesFilter, NO_FILTER, SelectionNote, type SiteFilter } from "../components/DeviceFilters";
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
  const [steps, setSteps] = useState<string[]>([""]);       // bundle id per step, flashed in order
  const [openStep, setOpenStep] = useState<number | null>(0);
  const [stepDelay, setStepDelay] = useState(15);
  const [waitOnline, setWaitOnline] = useState(true);
  const [onlineTimeout, setOnlineTimeout] = useState(180);
  const [typeTargets, setTypeTargets] = useState<Record<string, string[]>>({});
  const [allowMismatch, setAllowMismatch] = useState(false);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [chunk, setChunk] = useState(4096);
  const [concurrency, setConcurrency] = useState(1);
  const [stopOnFailure, setStopOnFailure] = useState(true);
  const [confirm, setConfirm] = useState(false);
  const [filter, setFilter] = useState("");
  const [siteFilter, setSiteFilter] = useState<SiteFilter>(NO_FILTER);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => {
    api.getConfig().then((c) => { setChunk(c.ota.chunk_size); setTypeTargets(c.ota.type_targets ?? {}); }).catch(() => undefined);
  }, []);
  // drop selections of bundles removed from the library
  useEffect(() => { setSteps((cur) => cur.map((id) => (library.some((b) => b.id === id) ? id : ""))); }, [library]);
  useEffect(() => {
    if (preselect.length) { setSelected(new Set(preselect)); onPreselectUsed(); }
  }, [preselect, onPreselectUsed]);
  useEffect(() => { setConfirm(false); setAllowMismatch(false); }, [steps, selected.size]);

  const chosen = steps.map((id) => library.find((b) => b.id === id));
  const ready = chosen.length > 0 && chosen.every(Boolean);
  const fw = chosen[0];
  const title = chosen.filter(Boolean).map((b) => `${b!.name} v${b!.version}`).join(" → ");
  // display-tap order check: boot → part → fw
  const ORDER = { boot: 0, part: 1, fw: 2 } as const;
  const kinds = chosen.map((b) => (b ? dtKind(b.name) : null));
  const dtOrderBad = kinds.some((k, i) => k && kinds.slice(i + 1).some((k2) => k2 && ORDER[k2] < ORDER[k]));
  // complete DT sets in the library (same version has boot, part and fw)
  const dtSets = useMemo(() => {
    const byVer: Record<string, Partial<Record<"boot" | "part" | "fw", string>>> = {};
    for (const b of library) { const k = dtKind(b.name); if (k) (byVer[b.version] ??= {})[k] = b.id; }
    return Object.entries(byVer).filter(([, s]) => s.boot && s.part && s.fw)
      .sort(([a], [b]) => compareVersions(b, a)).map(([v, s]) => ({ version: v, ids: [s.boot!, s.part!, s.fw!] }));
  }, [library]);
  const setStep = (i: number, id: string) => { const n = [...steps]; n[i] = id; setSteps(n); setOpenStep(null); };
  const rowFor = (d: Device) => fleet[d.id];
  const siteOf = (d: Device) => ({ device_type: d.device_type, shed: d.shed, pump_type: d.pump_type,
    board_version: d.board_version || fleet[d.id]?.info.hw_version });
  /** bundle targets the device's type does not accept (Settings → OTA → targets per device type) */
  const misfits = (d: Device): string[] => {
    const pats = Object.entries(typeTargets).find(([t]) => t.toLowerCase() === (d.device_type ?? "").toLowerCase())?.[1] ?? [];
    if (!pats.length) return [];
    return chosen.filter(Boolean).map((b) => b!.name).filter((t) => !pats.some((p) => globMatch(t, p)));
  };
  const f = filter.trim().toLowerCase();
  const rows = useMemo(() => devices
    .filter((d) => !f || [d.label, d.group, d.mac, d.uuid, d.shed, d.pump_id_1, d.pump_id_2].some((x) => (x ?? "").toLowerCase().includes(f)))
    .filter((d) => matchesFilter(siteOf(d), siteFilter))
    .sort((a, b) => Number(!a.shed) - Number(!b.shed) || a.shed.localeCompare(b.shed) || a.label.localeCompare(b.label)),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [devices, f, siteFilter, fleet]);
  const allRows = useMemo(() => devices.map(siteOf),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [devices, fleet]);
  const visibleIds = new Set(rows.map((d) => d.id));
  const allSel = rows.length > 0 && rows.every((d) => selected.has(d.id));
  const toggleVisible = () => {
    const n = new Set(selected);
    rows.forEach((d) => (allSel ? n.delete(d.id) : n.add(d.id)));
    setSelected(n);
  };
  const hiddenSelected = [...selected].filter((id) => !visibleIds.has(id)).length;
  const mismatched = devices.filter((d) => selected.has(d.id) && misfits(d).length > 0);

  const start = async () => {
    if (!confirm) { setConfirm(true); return; }
    setConfirm(false); setMsg(null);
    try {
      await api.startBatch({ firmware_ids: steps, device_ids: [...selected], chunk_size: chunk, concurrency,
        stop_on_failure: stopOnFailure, step_delay_s: stepDelay, wait_online: waitOnline, online_timeout_s: onlineTimeout,
        allow_type_mismatch: allowMismatch });
      setMsg({ ok: true, text: `Started — progress below` });
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  return (
    <section className="card">
      <div className="card-head">
        <h3>Flash devices</h3>
        {dtSets.length > 0 && (
          <div className="row wrap">
            <span className="muted small">Display-tap set:</span>
            {dtSets.slice(0, 4).map((s) => (
              <button key={s.version} className="btn small" title="Fill steps 1–3 with boot → part → fw"
                      onClick={() => { setSteps(s.ids); setOpenStep(null); }}>v{s.version}</button>
            ))}
          </div>
        )}
      </div>
      <div className="steps">
        {steps.map((id, i) => {
          const b = chosen[i];
          return (
            <div key={i} className={`step ${openStep === i ? "open" : ""}`}>
              <div className="step-head">
                <span className="step-no">Step {i + 1}</span>
                {b ? <span><b>{b.name}</b> <code>v{b.version}</code> <span className="muted small">{b.filename}</span></span>
                   : <span className="muted">no bundle chosen</span>}
                <span className="spacer" />
                <button className="btn small" onClick={() => setOpenStep(openStep === i ? null : i)}>{openStep === i ? "Done" : b ? "Change" : "Choose"}</button>
                {i > 0 && i === steps.length - 1 && (
                  <button className="btn small ghost" onClick={() => { setSteps(steps.slice(0, -1)); setOpenStep(null); }}>Remove</button>
                )}
              </div>
              {openStep === i && <BundlePicker library={library} value={id} onChange={(v) => setStep(i, v)} name={`step${i}`} />}
            </div>
          );
        })}
        <div className="row wrap">
          {steps.length < 3 && <button className="btn small ghost" onClick={() => { setSteps([...steps, ""]); setOpenStep(steps.length); }}>+ Add step {steps.length + 1}</button>}
          {library.length === 0 && <span className="muted small">The library is empty — upload or import bundles above.</span>}
          {dtOrderBad && <span className="warn-text">Display-tap bundles are normally flashed boot → part → fw — check the step order.</span>}
        </div>
      </div>
      {steps.length > 1 && (
        <div className="form-grid">
          <label className="field"><span>Pause after each step (s)</span>
            <input type="number" min={0} value={stepDelay} onChange={(e) => setStepDelay(Number(e.target.value) || 0)} /></label>
          <label className="field"><span>Wait for the device up to (s)</span>
            <input type="number" min={10} value={onlineTimeout} disabled={!waitOnline} onChange={(e) => setOnlineTimeout(Number(e.target.value) || 10)} /></label>
          <label className="check field-check">
            <input type="checkbox" checked={waitOnline} onChange={(e) => setWaitOnline(e.target.checked)} />
            Before the next step, wait until the device answers again (it reboots after each OTA)
          </label>
        </div>
      )}
      <div className="form-grid">
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
      <div className="row wrap filter-bar">
        <input className="search small" placeholder="Search devices…" value={filter} onChange={(e) => setFilter(e.target.value)} />
        <DeviceFilters rows={allRows} value={siteFilter} onChange={setSiteFilter} />
      </div>
      <SelectionNote selected={selected} visibleIds={visibleIds}
                     onUnselectHidden={() => setSelected(new Set([...selected].filter((id) => visibleIds.has(id))))}
                     onClear={() => setSelected(new Set())} />
      <div className="table-wrap">
        <table className="list">
          <thead><tr>
            <th><input type="checkbox" checked={allSel} aria-label="Select all shown" title="Select / unselect the rows shown"
                       onChange={toggleVisible} /></th>
            <th>Device</th><th>Type</th><th>Shed</th><th>Pump type</th><th>Board</th><th>Status</th><th>FW now</th><th>OTA</th>
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
                  <td>{d.device_type ? <span className={`type-badge t-${d.device_type.toLowerCase()}`}>{d.device_type}</span> : <span className="muted">—</span>}
                    {misfits(d).length > 0 && <div className="warn-text small" title={`Not for ${d.device_type}: ${misfits(d).join(", ")}`}>⚠ bundle mismatch</div>}</td>
                  <td>{d.shed || <span className="muted">—</span>}</td>
                  <td>{d.pump_type || <span className="muted">—</span>}</td>
                  <td>{siteOf(d).board_version || <span className="muted">—</span>}</td>
                  <td><span className={`dot ${online ? "connected" : fr?.last_seen ? "disconnected" : ""}`} /> {fr?.last_seen ? fmtAgo(fr.last_seen, now) : "never seen"}</td>
                  <td><code>{fr?.info.fw_version ?? "—"}</code></td>
                  <td>{s ? <span className={s.state === "succeeded" ? "ok-text" : s.state === "running" ? "" : "inline-error"}>
                    {s.state}{s.state === "running" ? ` ${s.progress}%` : ""}</span> : <span className="muted">—</span>}</td>
                </tr>
              );
            })}
            {rows.length === 0 && <tr><td colSpan={9} className="muted center">No devices match the filters.</td></tr>}
          </tbody>
        </table>
      </div>
      <div className="row wrap flash-bar">
        {mismatched.length > 0 && (
          <span className="warn-text">
            {mismatched.length} selected device(s) don't take {mismatched.length === 1 ? "this bundle" : "these bundles"} by type
            ({mismatched.slice(0, 3).map((d) => `${d.label} (${d.device_type})`).join(", ")}{mismatched.length > 3 ? "…" : ""})
            <label className="check inline-check"><input type="checkbox" checked={allowMismatch} onChange={(e) => setAllowMismatch(e.target.checked)} /> flash anyway</label>
          </span>
        )}
        <button className={`btn ${confirm ? "danger" : "primary"}`} disabled={!ready || !fw || selected.size === 0 || (mismatched.length > 0 && !allowMismatch)} onClick={start}>
          {confirm ? `Confirm: ${title} on ${selected.size} device(s)` : `Start OTA${steps.length > 1 ? ` (${steps.length} steps)` : ""} on ${selected.size} device(s)`}
        </button>
        {!ready && <span className="muted small">Choose a bundle for every step</span>}
        {confirm && hiddenSelected > 0 &&
          <span className="warn-text">Includes {hiddenSelected} device(s) not shown by the current filters</span>}
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
              <b>{b.title ?? `${b.target} v${b.version}`}</b>
              <span className={STATE_CLASS[b.state]}>{b.state}</span>
              <span className="muted small">{done}/{b.items.length} done · {b.concurrency} at a time · {fmtDateTime(b.started)} by {b.started_by}</span>
              {b.state === "running" && <button className="btn small danger" onClick={() => api.cancelBatch(b.batch_id)}>Cancel</button>}
            </div>
            <div className="batch-items">
              {b.items.map((i) => {
                const s = ota[i.device_id];
                const n = b.steps?.length ?? 1;
                const cur = i.step ?? 0;
                const stepState = i.steps?.[cur]?.state;
                const stepPct = stepState === "running" ? s?.progress ?? 0 : stepState === "succeeded" ? 100 : 0;
                const pct = i.state === "succeeded" ? 100 : Math.round(((cur + stepPct / 100) / n) * 100);
                const label = i.state === "running"
                  ? (n > 1 ? `step ${cur + 1}/${n} ${stepState === "waiting" ? "waiting for device" : `${stepPct}%`}` : `${stepPct}%`)
                  : i.state;
                return (
                  <button key={i.device_id} className={`batch-item ${logDevice === i.device_id ? "active" : ""}`}
                          onClick={() => setLogDevice(i.device_id)} title={i.error ?? "Show log"}>
                    <span className="batch-label">{i.label}</span>
                    <progress max={100} value={pct} />
                    <span className={`small ${STATE_CLASS[i.state] ?? ""}`}>{label}</span>
                    {n > 1 && (
                      <span className="step-dots">
                        {(i.steps ?? []).map((st, k) => <span key={k} className={`dot-step ${st.state}`} title={`Step ${k + 1}: ${b.steps?.[k]?.target} — ${st.state}`} />)}
                      </span>
                    )}
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

/** Case-insensitive glob match ("esp07-*", "*printer*"). */
function globMatch(text: string, pattern: string): boolean {
  const re = new RegExp("^" + pattern.trim().toLowerCase().replace(/[.+^${}()|[\]\\]/g, "\\$&").replace(/\*/g, ".*").replace(/\?/g, ".") + "$");
  return re.test(text.toLowerCase());
}
