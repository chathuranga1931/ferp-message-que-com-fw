import { useCallback, useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { useAuth } from "../auth";
import { HarnessTags } from "../components/HarnessTags";
import type { Device, DeviceLogEntry, DeviceLogIn, Harness, LogKind, Shed, Snapshot } from "../types";
import { fmtDateTime } from "../util";

const KINDS: { value: LogKind; label: string }[] = [
  { value: "issue", label: "Issue reported" },
  { value: "repair", label: "Repair / fix" },
  { value: "modification", label: "Modification" },
  { value: "config", label: "Config change" },
  { value: "manufactured", label: "Manufactured" },
  { value: "delivered", label: "Delivered" },
  { value: "note", label: "Note" },
  { value: "ota", label: "OTA" },
];
const KIND_LABEL = Object.fromEntries(KINDS.map((k) => [k.value, k.label])) as Record<LogKind, string>;
/** kinds that can be added by hand (OTA entries are written by the server when an OTA ends) */
const MANUAL_KINDS = KINDS.filter((k) => k.value !== "ota");

const today = () => new Date().toISOString().slice(0, 10);
const BLANK_ENTRY = (): DeviceLogIn => ({ date: today(), kind: "issue", title: "", details: "", status: "", snapshot_id: "" });

/** delivered + months → YYYY-MM-DD (empty when no delivery date). */
function warrantyEnd(delivered: string, months: number): string {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(delivered)) return "";
  const d = new Date(`${delivered}T00:00:00`);
  d.setMonth(d.getMonth() + months);
  return d.toISOString().slice(0, 10);
}

interface Props {
  device: Device;
  /** which part of the device record to show (tabs of the device pop-up) */
  section: "lifecycle" | "history";
  onSheds: (name?: string) => void; onChanged: () => void;
  /** open-issue count, for the tab label */
  onOpenIssues?: (n: number) => void;
}

/** Lifecycle (dates, warranty, cables, shed, config snapshot) and History of one device. */
export function DeviceRecords({ device, section, onSheds, onChanged, onOpenIssues }: Props) {
  const deviceId = device.id;
  const canEdit = useAuth().can("devices");

  const [log, setLog] = useState<DeviceLogEntry[]>([]);
  const [sheds, setSheds] = useState<Shed[]>([]);
  const [harnesses, setHarnesses] = useState<Harness[]>([]);
  const [snapshots, setSnapshots] = useState<Snapshot[]>([]);
  const [life, setLife] = useState({ manufactured: "", delivered: "", warranty_months: 12 });
  const [entry, setEntry] = useState<DeviceLogIn>(BLANK_ENTRY);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [confirmDel, setConfirmDel] = useState<string | null>(null);
  const [kindFilter, setKindFilter] = useState<"" | LogKind>("");
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const reloadLog = useCallback(() => {
    if (deviceId) api.deviceLog(deviceId).then(setLog).catch((e) => setMsg({ ok: false, text: e.message }));
  }, [deviceId]);

  useEffect(() => {
    api.sheds().then(setSheds).catch(() => undefined);
    api.harnesses().then(setHarnesses).catch(() => undefined);
    api.snapshots().then(setSnapshots).catch(() => undefined);
  }, []);
  useEffect(() => { reloadLog(); setEntry(BLANK_ENTRY()); setEditingId(null); setMsg(null); }, [reloadLog]);
  useEffect(() => {
    if (device) setLife({ manufactured: device.manufactured ?? "", delivered: device.delivered ?? "", warranty_months: device.warranty_months ?? 12 });
  }, [device]);

  const shed = useMemo(() => sheds.find((s) => s.name.trim().toLowerCase() === (device?.shed ?? "").trim().toLowerCase()), [sheds, device]);
  const snap = snapshots.find((s) => s.id === device?.config_snapshot);
  const lastConfig = log.find((e) => e.kind === "config" && e.snapshot_id && e.snapshot_id === device?.config_snapshot);
  const openIssues = log.filter((e) => e.kind === "issue" && e.status === "open").length;
  useEffect(() => { onOpenIssues?.(openIssues); }, [openIssues, onOpenIssues]);

  const wEnd = warrantyEnd(life.delivered, life.warranty_months);
  const wDays = wEnd ? Math.ceil((new Date(`${wEnd}T00:00:00`).getTime() - Date.now()) / 86400000) : null;
  const lifeChanged = life.manufactured !== (device.manufactured ?? "") || life.delivered !== (device.delivered ?? "")
    || life.warranty_months !== (device.warranty_months ?? 12);

  const saveDevice = async (changes: Partial<Device>, ok: string) => {
    setMsg(null);
    try {
      await api.updateDevice(device.id, { ...device, ...changes });
      onChanged();
      setMsg({ ok: true, text: ok });
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const saveEntry = async () => {
    if (!entry.title.trim()) { setMsg({ ok: false, text: "Give the entry a title" }); return; }
    setMsg(null);
    try {
      if (editingId) await api.updateDeviceLog(editingId, entry);
      else await api.addDeviceLog(device.id, entry);
      setEntry(BLANK_ENTRY()); setEditingId(null);
      reloadLog(); onChanged();
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const setIssueStatus = async (e: DeviceLogEntry, status: "open" | "resolved") => {
    await api.updateDeviceLog(e.id, { ...e, status }).catch((err) => setMsg({ ok: false, text: err.message }));
    reloadLog();
  };

  const remove = async (id: string) => {
    if (confirmDel !== id) { setConfirmDel(id); return; }
    setConfirmDel(null);
    await api.deleteDeviceLog(id).catch((err) => setMsg({ ok: false, text: err.message }));
    reloadLog();
  };

  const shown = log.filter((e) => !kindFilter || e.kind === kindFilter);

  return (
    <div className="stack">
      {msg && <div className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</div>}
      {section === "lifecycle" && <div className="device-grid">
        <section className="card">
          <div className="card-head">
            <h3>Lifecycle</h3>
            <button className="btn small primary" disabled={!lifeChanged || !canEdit}
                    onClick={() => saveDevice(life, "Lifecycle saved")}>Save</button>
          </div>
          <div className="form-grid">
            <label className="field"><span>Manufactured</span>
              <input type="date" value={life.manufactured} onChange={(e) => setLife({ ...life, manufactured: e.target.value })} /></label>
            <label className="field"><span>Delivered to customer</span>
              <input type="date" value={life.delivered} onChange={(e) => setLife({ ...life, delivered: e.target.value })} /></label>
            <label className="field"><span>Warranty (months)</span>
              <input type="number" min={0} max={120} value={life.warranty_months}
                     onChange={(e) => setLife({ ...life, warranty_months: Math.max(0, Number(e.target.value) || 0) })} /></label>
          </div>
          <div>
            {wEnd ? (
              <span className={`pill ${wDays! < 0 ? "err" : wDays! <= 30 ? "warn" : "ok"}`}>
                {wDays! < 0 ? `Warranty expired ${wEnd}` : `Under warranty until ${wEnd} (${wDays} days)`}
              </span>
            ) : <span className="muted small">Set the delivery date to track the warranty.</span>}
          </div>
        </section>

        <section className="card">
          <div className="card-head"><h3>Cables / harnesses</h3><span className="muted small">Saved immediately</span></div>
          <HarnessTags value={device.harnesses ?? []} catalog={harnesses}
                       onChange={(ids) => saveDevice({ harnesses: ids }, "Cables saved")} />
        </section>

        <section className="card">
          <div className="card-head">
            <h3>Shed</h3>
            {device.shed && <button className="btn small ghost" onClick={() => onSheds(device.shed)}>{shed ? "Edit shed" : "Add shed details"}</button>}
          </div>
          {!device.shed ? <span className="muted small">No shed set for this device.</span>
            : !shed ? <span className="muted small">No details recorded for “{device.shed}” yet.</span>
            : (
              <dl className="kv">
                <dt>Name</dt><dd>{shed.name}</dd>
                {shed.customer && <><dt>Customer</dt><dd>{shed.customer}</dd></>}
                {shed.contact_name && <><dt>Contact</dt><dd>{shed.contact_name}</dd></>}
                {(shed.phone || shed.phone_2) && <><dt>Phone</dt><dd>{[shed.phone, shed.phone_2].filter(Boolean).join(" · ")}</dd></>}
                {shed.email && <><dt>E-mail</dt><dd>{shed.email}</dd></>}
                {(shed.address || shed.city) && <><dt>Address</dt><dd className="pre">{[shed.address, shed.city].filter(Boolean).join("\n")}</dd></>}
                {shed.map_url && <><dt>Map</dt><dd><a href={shed.map_url} target="_blank" rel="noreferrer">Open map</a></dd></>}
                {shed.remote_access.length > 0 && <><dt>Remote</dt><dd>
                  {shed.remote_access.map((r, i) => <div key={i}><b>{r.tool}</b> <code>{r.address}</code>{r.notes && <span className="muted small"> — {r.notes}</span>}</div>)}
                </dd></>}
              </dl>
            )}
        </section>

        <section className="card">
          <div className="card-head"><h3>Config snapshot</h3></div>
          {snap ? (
            <div>
              <b>{snap.name}</b> <span className="muted small">{Object.keys(snap.values).length} keys</span>
              {lastConfig && <div className="muted small">applied {lastConfig.date}{lastConfig.user ? ` by ${lastConfig.user}` : ""}</div>}
            </div>
          ) : <span className="muted small">{device.config_snapshot ? "The recorded snapshot was deleted." : "No snapshot recorded. Applying one from the Snapshots page records it here."}</span>}
          <label className="field">
            <span>Set loaded snapshot</span>
            <select value={device.config_snapshot ?? ""} onChange={(e) => {
              const s = snapshots.find((x) => x.id === e.target.value);
              if (s) api.addDeviceLog(device.id, { ...BLANK_ENTRY(), kind: "config", title: `Config snapshot "${s.name}" loaded`, snapshot_id: s.id })
                .then(() => { reloadLog(); onChanged(); }).catch((err) => setMsg({ ok: false, text: err.message }));
              else saveDevice({ config_snapshot: "" }, "Snapshot cleared");
            }}>
              <option value="">— none —</option>
              {snapshots.map((s) => <option key={s.id} value={s.id}>{s.name}</option>)}
            </select>
          </label>
        </section>
      </div>}

      {section === "history" && <section className="card">
        <div className="card-head">
          <h3>History</h3>
          <select value={kindFilter} onChange={(e) => setKindFilter(e.target.value as typeof kindFilter)}>
            <option value="">All entries ({log.length})</option>
            {KINDS.map((k) => <option key={k.value} value={k.value}>{k.label} ({log.filter((e) => e.kind === k.value).length})</option>)}
          </select>
        </div>

        {canEdit && <div className="edit-box log-form">
          <div className="form-grid">
            <label className="field"><span>Date</span><input type="date" value={entry.date} onChange={(e) => setEntry({ ...entry, date: e.target.value })} /></label>
            <label className="field"><span>Kind</span>
              <select value={entry.kind} onChange={(e) => setEntry({ ...entry, kind: e.target.value as LogKind })}>
                {MANUAL_KINDS.map((k) => <option key={k.value} value={k.value}>{k.label}</option>)}
              </select></label>
            {entry.kind === "issue" && (
              <label className="field"><span>Status</span>
                <select value={entry.status || "open"} onChange={(e) => setEntry({ ...entry, status: e.target.value as "open" | "resolved" })}>
                  <option value="open">Open</option><option value="resolved">Resolved</option>
                </select></label>
            )}
          </div>
          <label className="field"><span>Title</span>
            <input value={entry.title} placeholder="e.g. Display tap not reading after power cut" onChange={(e) => setEntry({ ...entry, title: e.target.value })} /></label>
          <label className="field"><span>Details</span>
            <textarea rows={3} value={entry.details} placeholder="What was reported / done, parts replaced, cables changed…"
                      onChange={(e) => setEntry({ ...entry, details: e.target.value })} /></label>
          <div className="row">
            <button className="btn small primary" onClick={saveEntry}>{editingId ? "Save entry" : "Add entry"}</button>
            {editingId && <button className="btn small ghost" onClick={() => { setEditingId(null); setEntry(BLANK_ENTRY()); }}>Cancel</button>}
          </div>
        </div>}

        <div className="timeline">
          {shown.map((e) => (
            <div key={e.id} className={`tl-item k-${e.kind}${e.kind === "issue" ? ` s-${e.status || "open"}` : ""}${e.kind === "ota" ? ` r-${e.ota_result || "failed"}` : ""}`}>
              <div className="tl-date">{e.date}</div>
              <div className="tl-body">
                <div className="tl-head">
                  <span className={`kind-badge k-${e.kind}`}>{KIND_LABEL[e.kind] ?? e.kind}</span>
                  <b>{e.title}</b>
                  {e.kind === "issue" && <span className={`pill ${e.status === "resolved" ? "ok" : "err"}`}>{e.status || "open"}</span>}
                  {e.kind === "ota" && e.ota_result && <span className={`pill ${e.ota_result === "succeeded" ? "ok" : "err"}`}>{e.ota_result}</span>}
                </div>
                {e.details && <div className="pre small">{e.details}</div>}
                <div className="muted small">{e.user || "—"} · {fmtDateTime(e.created)}</div>
              </div>
              {canEdit && <div className="tl-actions">
                {e.kind === "issue" && (e.status === "resolved"
                  ? <button className="btn small ghost" onClick={() => setIssueStatus(e, "open")}>Reopen</button>
                  : <button className="btn small" onClick={() => setIssueStatus(e, "resolved")}>Resolve</button>)}
                <button className="btn small ghost" onClick={() => { setEditingId(e.id); setEntry({ date: e.date, kind: e.kind, title: e.title, details: e.details, status: e.status, snapshot_id: e.snapshot_id }); }}>Edit</button>
                <button className={`btn small ${confirmDel === e.id ? "danger" : "ghost"}`} onClick={() => remove(e.id)}>
                  {confirmDel === e.id ? "Confirm delete" : "Delete"}</button>
              </div>}
            </div>
          ))}
          {shown.length === 0 && <div className="muted small">No history yet.</div>}
        </div>
      </section>}
    </div>
  );
}
