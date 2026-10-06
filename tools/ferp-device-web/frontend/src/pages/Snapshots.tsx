import { useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import { DeviceFilters, matchesFilter, NO_FILTER, SelectionNote, type SiteFilter } from "../components/DeviceFilters";
import { seedDevice, useLive, waitForJob } from "../store";
import type { Catalog, Device, Snapshot } from "../types";
import { downloadText, EMPTY, fmtDateTime, hex } from "../util";

interface Props { catalog: Catalog | null; devices: Device[] }

export default function Snapshots({ catalog, devices }: Props) {
  const [list, setList] = useState<Snapshot[]>([]);
  const [selId, setSelId] = useState("");
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const fileRef = useRef<HTMLInputElement>(null);

  const refresh = () => api.snapshots().then((l) => { setList(l); setSelId((cur) => (l.some((s) => s.id === cur) ? cur : l[0]?.id ?? "")); });
  useEffect(() => { refresh().catch((e) => setMsg({ ok: false, text: e.message })); }, []);
  const sel = list.find((s) => s.id === selId) ?? null;

  const importFile = async (f: File) => {
    setMsg(null);
    try {
      const j = JSON.parse(await f.text());
      const values: Record<string, string> = {};
      for (const [k, v] of Object.entries(j.values ?? {})) values[k] = typeof v === "object" && v ? String((v as { value: unknown }).value) : String(v);
      const s = await api.createSnapshot({ name: j.name || f.name.replace(/\.json$/i, ""), notes: j.notes ?? "imported", values });
      await refresh(); setSelId(s.id);
      setMsg({ ok: true, text: `Imported "${s.name}" (${Object.keys(s.values).length} keys)` });
    } catch (e) { setMsg({ ok: false, text: `Import failed: ${(e as Error).message}` }); }
    finally { if (fileRef.current) fileRef.current.value = ""; }
  };

  return (
    <div className="stack">
      <CreateSnapshot devices={devices} onCreated={async (s) => { await refresh(); setSelId(s.id); }} />
      <div className="snap-grid">
        <section className="card">
          <div className="card-head">
            <h3>Snapshots <span className="muted">({list.length})</span></h3>
            <input ref={fileRef} type="file" accept=".json" hidden onChange={(e) => e.target.files?.[0] && importFile(e.target.files[0])} />
            <button className="btn small" onClick={() => fileRef.current?.click()}>Import .json</button>
          </div>
          {msg && <div className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</div>}
          <div className="snap-list">
            {list.map((s) => (
              <button key={s.id} className={`snap-item ${s.id === selId ? "active" : ""}`} onClick={() => setSelId(s.id)}>
                <b>{s.name}</b>
                <span className="muted small">{Object.keys(s.values).length} keys · {s.source ? s.source.label : "imported"} · {fmtDateTime(s.created)}</span>
              </button>
            ))}
            {list.length === 0 && <p className="muted">No snapshots yet. Save one from a device above.</p>}
          </div>
        </section>
        {sel && catalog ? <SnapshotDetail snap={sel} devices={devices} onDeleted={refresh} />
                        : <section className="card empty">Select a snapshot.</section>}
      </div>
    </div>
  );
}

function CreateSnapshot({ devices, onCreated }: { devices: Device[]; onCreated: (s: Snapshot) => void }) {
  const [deviceId, setDeviceId] = useState("");
  const [name, setName] = useState("");
  const [busy, setBusy] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const connected = useLive((s) => s.mqtt?.connected ?? false);

  const save = async () => {
    setBusy(true); setMsg(null);
    try {
      const { job_id } = await api.readConfig(deviceId);
      setMsg({ ok: true, text: "Reading config from device…" });
      const job = await waitForJob(job_id);
      if (!job.ok) throw new Error(job.errors?.[0] ?? "Read failed");
      const s = await api.createSnapshot({ name: name.trim(), device_id: deviceId });
      setMsg({ ok: true, text: `Saved "${s.name}" with ${Object.keys(s.values).length} keys` });
      setName("");
      onCreated(s);
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
    finally { setBusy(false); }
  };

  return (
    <section className="card">
      <div className="card-head"><h3>New snapshot from a device</h3></div>
      <div className="row wrap">
        <select value={deviceId} onChange={(e) => setDeviceId(e.target.value)}>
          <option value="">— device —</option>
          {devices.map((d) => <option key={d.id} value={d.id}>{d.label}</option>)}
        </select>
        <input placeholder="Snapshot name" value={name} onChange={(e) => setName(e.target.value)} />
        <button className="btn primary" disabled={!deviceId || !name.trim() || busy || !connected} onClick={save}>
          {busy ? "Working…" : "Read config & save"}</button>
        {msg && <span className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</span>}
      </div>
    </section>
  );
}

function SnapshotDetail({ snap, devices, onDeleted }: { snap: Snapshot; devices: Device[]; onDeleted: () => void }) {
  const [compareId, setCompareId] = useState("");
  const [onlyDiff, setOnlyDiff] = useState(false);
  const [targets, setTargets] = useState<Set<string>>(new Set());
  const [siteFilter, setSiteFilter] = useState<SiteFilter>(NO_FILTER);
  const [search, setSearch] = useState("");
  const [applyOnlyDiff, setApplyOnlyDiff] = useState(true);
  const [confirm, setConfirm] = useState<"apply" | "delete" | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const connected = useLive((s) => s.mqtt?.connected ?? false);
  const cmpValues = useLive((s) => s.config[compareId]) ?? EMPTY;

  useEffect(() => { setConfirm(null); setMsg(null); }, [snap.id]);
  useEffect(() => {      // load the compared device's last-read values
    if (compareId) api.deviceState(compareId).then((s) => seedDevice(compareId, s.config, s.devinfo, s.jobs, s.ota)).catch(() => undefined);
  }, [compareId]);

  // Apply-to-devices list: Type / Shed / Pump type / Board filters + text search
  const shownDevices = useMemo(() => {
    const q = search.trim().toLowerCase();
    return devices
      .filter((d) => matchesFilter(d, siteFilter))
      .filter((d) => !q || [d.label, d.mac, d.shed, d.pump_id_1, d.pump_id_2].some((x) => (x ?? "").toLowerCase().includes(q)));
  }, [devices, siteFilter, search]);
  const shownIds = useMemo(() => new Set(shownDevices.map((d) => d.id)), [shownDevices]);
  const allShownSelected = shownDevices.length > 0 && shownDevices.every((d) => targets.has(d.id));
  const toggleShown = () => {
    const n = new Set(targets);
    shownDevices.forEach((d) => (allShownSelected ? n.delete(d.id) : n.add(d.id)));
    setTargets(n);
  };

  const rows = useMemo(() => Object.entries(snap.values).map(([k, v]) => {
    const keyId = parseInt(k, 16);
    const other = compareId ? cmpValues[String(keyId)]?.value : undefined;
    return { k, keyId, ...v, other, differs: compareId !== "" && other !== v.value };
  }).filter((r) => !onlyDiff || r.differs), [snap, compareId, cmpValues, onlyDiff]);
  const diffCount = compareId ? Object.entries(snap.values).filter(([k, v]) => cmpValues[String(parseInt(k, 16))]?.value !== v.value).length : 0;

  const apply = async () => {
    if (confirm !== "apply") { setConfirm("apply"); return; }
    setConfirm(null); setMsg(null);
    try {
      const r = await api.applySnapshot(snap.id, [...targets], applyOnlyDiff);
      const queued = r.results.filter((x) => x.keys > 0);
      setMsg({ ok: true, text: queued.length
        ? `Writing ${queued.map((x) => `${x.keys} key(s) to ${devices.find((d) => d.id === x.device_id)?.label}`).join(", ")} — progress in the Workspace`
        : "Nothing to write — the selected devices already match (based on their last-read values)." });
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const del = async () => {
    if (confirm !== "delete") { setConfirm("delete"); return; }
    await api.deleteSnapshot(snap.id).catch((e) => setMsg({ ok: false, text: e.message }));
    onDeleted();
  };

  const toggle = (id: string) => { const n = new Set(targets); if (n.has(id)) n.delete(id); else n.add(id); setTargets(n); };

  return (
    <section className="card">
      <div className="card-head">
        <h3>{snap.name}</h3>
        <div className="row">
          <button className="btn small" onClick={() => downloadText(`${snap.name}.json`, JSON.stringify(snap, null, 2))}>Export</button>
          <button className={`btn small ${confirm === "delete" ? "danger" : "ghost"}`} onClick={del}>{confirm === "delete" ? "Confirm delete" : "Delete"}</button>
        </div>
      </div>
      <p className="muted small">
        {snap.source ? `From ${snap.source.label}` : "Imported"} · {fmtDateTime(snap.created)} by {snap.created_by}{snap.notes && ` · ${snap.notes}`}
      </p>

      <div className="row wrap">
        <label className="field inline"><span>Compare with</span>
          <select value={compareId} onChange={(e) => setCompareId(e.target.value)}>
            <option value="">— nothing —</option>
            {devices.map((d) => <option key={d.id} value={d.id}>{d.label} (last-read values)</option>)}
          </select>
        </label>
        {compareId && <span className={diffCount ? "inline-error" : "ok-text"}>{diffCount ? `${diffCount} difference(s)` : "identical"}</span>}
        {compareId && <label className="check"><input type="checkbox" checked={onlyDiff} onChange={(e) => setOnlyDiff(e.target.checked)} /> Only differences</label>}
      </div>
      {compareId && Object.keys(cmpValues).length === 0 &&
        <p className="muted small">No values read from this device yet — open it in the Workspace and use Read all.</p>}

      <div className="table-wrap">
        <table className="keys">
          <thead><tr><th>Key</th><th>Name</th><th>Type</th><th>Snapshot value</th>{compareId && <th>Device value</th>}</tr></thead>
          <tbody>
            {rows.map((r) => (
              <tr key={r.k} className={r.differs ? "modified" : ""}>
                <td><code>{hex(r.keyId)}</code></td><td>{r.name}</td><td className="muted">{r.type}</td>
                <td className="mono">{r.value}</td>
                {compareId && <td className="mono">{r.other ?? <span className="muted">not read</span>}</td>}
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div className="edit-box apply-box">
        <h4>Apply to devices</h4>
        <DeviceFilters rows={devices} value={siteFilter} onChange={setSiteFilter} />
        <div className="row wrap apply-tools">
          <input className="search small" placeholder="Search label / MAC / shed / pump…" value={search} onChange={(e) => setSearch(e.target.value)} />
          <label className="check small"><input type="checkbox" checked={allShownSelected} disabled={shownDevices.length === 0} onChange={toggleShown} />
            Select all shown ({shownDevices.length})</label>
        </div>
        <SelectionNote selected={targets} visibleIds={shownIds}
                       onUnselectHidden={() => setTargets(new Set([...targets].filter((id) => shownIds.has(id))))}
                       onClear={() => setTargets(new Set())} />
        <div className="device-checks">
          {shownDevices.map((d) => (
            <label key={d.id} className="check" title={[d.device_type, d.shed, d.board_version].filter(Boolean).join(" · ")}>
              <input type="checkbox" checked={targets.has(d.id)} onChange={() => toggle(d.id)} /> {d.label}
              {d.device_type && <span className={`type-badge t-${d.device_type.toLowerCase()}`}>{d.device_type}</span>}
              {d.shed && <span className="muted small"> {d.shed}</span>}
            </label>
          ))}
          {shownDevices.length === 0 && <span className="muted small">No devices match the filters.</span>}
        </div>
        <label className="check"><input type="checkbox" checked={applyOnlyDiff} onChange={(e) => setApplyOnlyDiff(e.target.checked)} />
          Only write keys that differ from each device's last-read value</label>
        <div className="row">
          <button className={`btn ${confirm === "apply" ? "danger" : "primary"}`} disabled={targets.size === 0 || !connected} onClick={apply}>
            {confirm === "apply" ? `Confirm: write "${snap.name}" to ${targets.size} device(s)` : "Apply"}</button>
          {confirm === "apply" && <button className="btn ghost" onClick={() => setConfirm(null)}>Cancel</button>}
          {msg && <span className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</span>}
        </div>
      </div>
    </section>
  );
}
