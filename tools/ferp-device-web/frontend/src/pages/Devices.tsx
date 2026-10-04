import { useMemo, useState } from "react";
import { api } from "../api";
import { DeviceFilters, matchesFilter, NO_FILTER, type SiteFilter } from "../components/DeviceFilters";
import { useLive } from "../store";
import type { Device, DeviceIn } from "../types";
import { topicId } from "../util";

const BLANK: DeviceIn = {
  label: "", mac: "", uuid: "", group: "default", ip: "", notes: "",
  shed: "", pump_id_1: "", pump_id_2: "", sd_card_size: "", board_version: "", pump_type: "", device_type: "COM",
};
const DEVICE_TYPES = ["COM", "Printer"];
const FIELDS = Object.keys(BLANK) as (keyof DeviceIn)[];
const SD_SIZES = ["4 GB", "8 GB", "16 GB", "32 GB", "64 GB"];
const PUMP_ID_MAX = 4;     // firmware NOZZLE_x_ID limit

/** Distinct non-empty values of a field across devices (for suggestions). */
function distinct(devices: Device[], k: keyof DeviceIn, extra: string[] = []): string[] {
  return [...new Set([...extra, ...devices.map((d) => d[k]).filter(Boolean)])].sort();
}

export default function Devices({ devices, onChanged, onLogs }:
  { devices: Device[]; onChanged: () => void; onLogs: (mac: string) => void }) {
  const [editing, setEditing] = useState<string | "new" | null>(null);
  const [form, setForm] = useState<DeviceIn>(BLANK);
  const [confirmDelete, setConfirmDelete] = useState<string | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [info, setInfo] = useState<string | null>(null);
  const [reading, setReading] = useState(false);
  const [filter, setFilter] = useState("");
  const [siteFilter, setSiteFilter] = useState<SiteFilter>(NO_FILTER);
  const connected = useLive((s) => s.mqtt?.connected ?? false);

  const open = (d: Device | null) => {
    setErr(null); setInfo(null);
    setEditing(d ? d.id : "new");
    setForm(d ? (Object.fromEntries(FIELDS.map((k) => [k, d[k] ?? ""])) as unknown as DeviceIn) : BLANK);
  };

  const save = async () => {
    if (!form.label.trim()) { setErr("Label is required"); return; }
    if (!form.mac.trim() && !form.uuid.trim()) { setErr("Enter a MAC or a UUID — MQTT topics need one of them"); return; }
    try {
      if (editing === "new") await api.addDevice(form);
      else if (editing) await api.updateDevice(editing, form);
      setEditing(null);
      onChanged();
    } catch (e) { setErr((e as Error).message); }
  };

  const readFromDevice = async () => {
    if (!editing || editing === "new") return;
    setReading(true); setErr(null); setInfo(null);
    try {
      const r = await api.readSiteInfo(editing);
      const got = Object.entries(r.values);
      setForm((f) => ({ ...f, ...r.values }));
      setInfo(got.length ? `Read from device: ${got.map(([k, v]) => `${k.replace(/_/g, " ")} = ${v || "(empty)"}`).join(", ")} — review and Save` : "");
      if (r.errors.length) setErr(r.errors.join("; "));
    } catch (e) { setErr((e as Error).message); }
    finally { setReading(false); }
  };

  const remove = async (id: string) => {
    if (confirmDelete !== id) { setConfirmDelete(id); return; }
    setConfirmDelete(null);
    await api.deleteDevice(id).catch((e) => setErr(e.message));
    onChanged();
  };

  const sheds = useMemo(() => distinct(devices, "shed"), [devices]);
  const f = filter.trim().toLowerCase();
  const rows = devices
    .filter((d) => matchesFilter(d, siteFilter))
    .filter((d) => !f || [d.label, d.mac, d.uuid, d.group, d.shed, d.pump_id_1, d.pump_id_2, d.pump_type, d.board_version, d.device_type, d.notes]
      .some((x) => (x ?? "").toLowerCase().includes(f)))
    .sort((a, b) => Number(!a.shed) - Number(!b.shed) || a.shed.localeCompare(b.shed) || a.label.localeCompare(b.label));
  const set = (k: keyof DeviceIn) => (e: React.ChangeEvent<HTMLInputElement>) => setForm({ ...form, [k]: e.target.value });
  const longId = [form.pump_id_1, form.pump_id_2].some((x) => x.length > PUMP_ID_MAX);

  return (
    <div className="stack">
      <section className="card">
        <div className="card-head">
          <h3>Devices <span className="muted">({devices.length})</span></h3>
          <div className="row wrap">
            <input className="search small" placeholder="Filter…" value={filter} onChange={(e) => setFilter(e.target.value)} />
            <button className="btn small primary" onClick={() => open(null)}>+ Add device</button>
          </div>
        </div>
        <DeviceFilters rows={devices} value={siteFilter} onChange={setSiteFilter} />
        {rows.length !== devices.length && <div className="muted small">Showing {rows.length} of {devices.length}</div>}
        {err && !editing && <div className="inline-error">{err}</div>}

        {editing && (
          <div className="edit-box">
            <h4>{editing === "new" ? "Add device" : "Edit device"}</h4>
            <div className="form-section">Identity</div>
            <div className="form-grid">
              <label className="field"><span>Label *</span><input value={form.label} onChange={set("label")} autoFocus /></label>
              <label className="field"><span>Device type</span>
                <input list="dl-types" value={form.device_type} onChange={set("device_type")} placeholder="COM / Printer" /></label>
              <label className="field"><span>MQTT group</span><input value={form.group} onChange={set("group")} /></label>
              <label className="field"><span>MAC</span><input value={form.mac} onChange={set("mac")} placeholder="f8b3b73988c8" /></label>
              <label className="field"><span>UUID (if provisioned)</span><input value={form.uuid} onChange={set("uuid")} /></label>
            </div>

            <div className="form-section">
              Site &amp; hardware
              {editing !== "new" && (
                <button className="btn small" disabled={!connected || reading} onClick={readFromDevice}
                        title="Reads pump IDs (NOZZLE_0/1_ID) and board version (HW_VERSION) from the device">
                  {reading ? "Reading…" : "Read from device"}</button>
              )}
            </div>
            <div className="form-grid">
              <label className="field"><span>Shed name</span>
                <input list="dl-sheds" value={form.shed} onChange={set("shed")} placeholder="e.g. YAKKALA" /></label>
              <label className="field"><span>Pump ID 1 (nozzle 1)</span>
                <input value={form.pump_id_1} onChange={set("pump_id_1")} placeholder="e.g. P01" /></label>
              <label className="field"><span>Pump ID 2 (nozzle 2)</span>
                <input value={form.pump_id_2} onChange={set("pump_id_2")} placeholder="e.g. P02" /></label>
              <label className="field"><span>Pump type</span>
                <input list="dl-pumptypes" value={form.pump_type} onChange={set("pump_type")} /></label>
              <label className="field"><span>Board version</span>
                <input list="dl-boards" value={form.board_version} onChange={set("board_version")} placeholder="e.g. V3" /></label>
              <label className="field"><span>SD card size</span>
                <input list="dl-sd" value={form.sd_card_size} onChange={set("sd_card_size")} placeholder="e.g. 8 GB" /></label>
              <label className="field wide"><span>Notes</span><input value={form.notes} onChange={set("notes")} /></label>
            </div>
            <datalist id="dl-sheds">{sheds.map((s) => <option key={s} value={s} />)}</datalist>
            <datalist id="dl-types">{distinct(devices, "device_type", DEVICE_TYPES).map((s) => <option key={s} value={s} />)}</datalist>
            <datalist id="dl-pumptypes">{distinct(devices, "pump_type").map((s) => <option key={s} value={s} />)}</datalist>
            <datalist id="dl-boards">{distinct(devices, "board_version", ["V2", "V3"]).map((s) => <option key={s} value={s} />)}</datalist>
            <datalist id="dl-sd">{distinct(devices, "sd_card_size", SD_SIZES).map((s) => <option key={s} value={s} />)}</datalist>

            {longId && <div className="inline-error small">The device stores at most {PUMP_ID_MAX} characters per pump ID.</div>}
            <p className="muted small">
              Topics: <code>ferp/&lt;dev_type&gt;/{form.group || "default"}/{topicId(form.uuid || form.mac) || "…"}/cmd</code>
              {" "}— UUID is used when set, otherwise the MAC. Pump IDs on the device itself are changed in the Workspace (config keys NOZZLE_0_ID / NOZZLE_1_ID).
            </p>
            {info && <div className="ok-text">{info}</div>}
            {err && <div className="inline-error">{err}</div>}
            <div className="row">
              <button className="btn primary" onClick={save}>Save</button>
              <button className="btn ghost" onClick={() => setEditing(null)}>Cancel</button>
            </div>
          </div>
        )}

        <div className="table-wrap tall">
          <table className="list devices-table">
            <thead><tr>
              <th>Label</th><th>Type</th><th>Shed</th><th>Pumps</th><th>Pump type</th><th>Board</th><th>SD</th>
              <th>Group</th><th>Topic id</th><th />
            </tr></thead>
            <tbody>
              {rows.map((d) => (
                <tr key={d.id}>
                  <td>{d.label}{d.notes && <div className="muted small">{d.notes}</div>}</td>
                  <td>{d.device_type ? <span className={`type-badge t-${d.device_type.toLowerCase()}`}>{d.device_type}</span> : <span className="muted">—</span>}</td>
                  <td>{d.shed || <span className="muted">—</span>}</td>
                  <td>{[d.pump_id_1, d.pump_id_2].filter(Boolean).join(" / ") || <span className="muted">—</span>}</td>
                  <td>{d.pump_type || <span className="muted">—</span>}</td>
                  <td>{d.board_version || <span className="muted">—</span>}</td>
                  <td>{d.sd_card_size || <span className="muted">—</span>}</td>
                  <td className="muted">{d.group}</td>
                  <td><code>{topicId(d.uuid || d.mac)}</code></td>
                  <td className="row-actions">
                    {topicId(d.mac) && <button className="btn small ghost" onClick={() => onLogs(topicId(d.mac))} title="UDP logs of this device (by MAC)">Logs</button>}
                    <button className="btn small" onClick={() => open(d)}>Edit</button>
                    <button className={`btn small ${confirmDelete === d.id ? "danger" : "ghost"}`} onClick={() => remove(d.id)}
                            onBlur={() => setConfirmDelete(null)}>{confirmDelete === d.id ? "Confirm delete" : "Delete"}</button>
                  </td>
                </tr>
              ))}
              {rows.length === 0 && <tr><td colSpan={10} className="muted center">No devices match.</td></tr>}
            </tbody>
          </table>
        </div>
      </section>
    </div>
  );
}
