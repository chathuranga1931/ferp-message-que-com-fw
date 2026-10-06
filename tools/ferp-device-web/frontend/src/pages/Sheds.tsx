import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import type { Device, Shed, ShedIn } from "../types";

const BLANK: ShedIn = {
  name: "", customer: "", contact_name: "", phone: "", phone_2: "", email: "",
  address: "", city: "", map_url: "", remote_access: [], notes: "",
};
const norm = (s: string) => s.trim().toLowerCase();

interface Props { devices: Device[]; focusName: string; onChanged: () => void; onOpenDevice: (id: string) => void }

/** Customer sites. Devices refer to a shed by name (the device's "Shed" field). */
export default function Sheds({ devices, focusName, onChanged, onOpenDevice }: Props) {
  const [sheds, setSheds] = useState<Shed[]>([]);
  const [selId, setSelId] = useState<string | "new" | null>(null);
  const [form, setForm] = useState<ShedIn>(BLANK);
  const [filter, setFilter] = useState("");
  const [confirmDel, setConfirmDel] = useState(false);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const reload = () => api.sheds().then(setSheds).catch((e) => setMsg({ ok: false, text: e.message }));
  useEffect(() => { reload(); }, []);

  const deviceCount = useMemo(() => {
    const m = new Map<string, number>();
    devices.forEach((d) => { if (d.shed) m.set(norm(d.shed), (m.get(norm(d.shed)) ?? 0) + 1); });
    return m;
  }, [devices]);
  // shed names used by devices that have no record yet
  const missing = useMemo(() => [...new Set(devices.map((d) => d.shed.trim()).filter(Boolean))]
    .filter((n) => norm(n) !== "unassigned" && !sheds.some((s) => norm(s.name) === norm(n))).sort(), [devices, sheds]);

  const open = (s: Shed | null, name = "") => {
    setMsg(null); setConfirmDel(false);
    if (s) { setSelId(s.id); setForm({ ...BLANK, ...s, remote_access: s.remote_access.map((r) => ({ ...r })) }); }
    else { setSelId("new"); setForm({ ...BLANK, name }); }
  };

  // Device page → "Add / edit shed details" lands here with the name pre-selected
  useEffect(() => {
    if (!focusName) return;
    const s = sheds.find((x) => norm(x.name) === norm(focusName));
    if (s) open(s); else if (sheds.length || !msg) open(null, focusName);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [focusName, sheds.length]);

  const save = async () => {
    setMsg(null);
    try {
      const r = selId === "new" ? await api.addShed(form) : await api.updateShed(selId!, form);
      await reload();
      setSelId(r.id);
      const renamed = "renamed_devices" in r ? (r as { renamed_devices: number }).renamed_devices : 0;
      if (renamed) onChanged();
      setMsg({ ok: true, text: renamed ? `Saved — ${renamed} device(s) moved to the new name` : "Saved" });
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const remove = async () => {
    if (!confirmDel) { setConfirmDel(true); return; }
    await api.deleteShed(selId as string).catch((e) => setMsg({ ok: false, text: e.message }));
    setSelId(null); setConfirmDel(false); reload();
  };

  const set = (k: keyof ShedIn) => (e: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>) => setForm({ ...form, [k]: e.target.value });
  const setRa = (i: number, k: "tool" | "address" | "notes", v: string) =>
    setForm({ ...form, remote_access: form.remote_access.map((r, j) => (j === i ? { ...r, [k]: v } : r)) });

  const f = norm(filter);
  const shown = sheds.filter((s) => !f || [s.name, s.customer, s.contact_name, s.city, s.phone].some((x) => norm(x).includes(f)));
  const shedDevices = selId && selId !== "new" ? devices.filter((d) => norm(d.shed) === norm(form.name)) : [];

  return (
    <div className="split">
      <section className="card list-pane">
        <div className="card-head">
          <h3>Sheds ({sheds.length})</h3>
          <button className="btn small primary" onClick={() => open(null)}>+ New shed</button>
        </div>
        <input className="search" placeholder="Filter…" value={filter} onChange={(e) => setFilter(e.target.value)} />
        <div className="pick-list">
          {shown.map((s) => (
            <button key={s.id} className={`pick ${selId === s.id ? "active" : ""}`} onClick={() => open(s)}>
              <b>{s.name}</b> <span className="muted small">{deviceCount.get(norm(s.name)) ?? 0} device(s)</span>
              <div className="muted small">{[s.customer, s.city, s.phone].filter(Boolean).join(" · ")}</div>
            </button>
          ))}
        </div>
        {missing.length > 0 && (
          <>
            <div className="small-label">Used by devices, no details yet</div>
            <div className="chip-list">
              {missing.map((n) => <button key={n} className="pick" onClick={() => open(null, n)}>+ {n}</button>)}
            </div>
          </>
        )}
      </section>

      <section className="card">
        {selId === null ? <div className="muted">Select a shed, or create one.</div> : (
          <>
            <div className="card-head">
              <h3>{selId === "new" ? "New shed" : form.name}</h3>
              <div className="row">
                <button className="btn small primary" disabled={!form.name.trim()} onClick={save}>Save</button>
                {selId !== "new" && <button className={`btn small ${confirmDel ? "danger" : "ghost"}`} onClick={remove}>
                  {confirmDel ? "Confirm delete" : "Delete"}</button>}
              </div>
            </div>
            {msg && <div className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</div>}
            <div className="form-grid">
              <label className="field"><span>Shed name (as on devices)</span><input value={form.name} onChange={set("name")} /></label>
              <label className="field"><span>Customer / owner</span><input value={form.customer} onChange={set("customer")} /></label>
              <label className="field"><span>Contact person</span><input value={form.contact_name} onChange={set("contact_name")} /></label>
              <label className="field"><span>Phone</span><input value={form.phone} onChange={set("phone")} /></label>
              <label className="field"><span>Phone 2</span><input value={form.phone_2} onChange={set("phone_2")} /></label>
              <label className="field"><span>E-mail</span><input value={form.email} onChange={set("email")} /></label>
              <label className="field"><span>City</span><input value={form.city} onChange={set("city")} /></label>
              <label className="field"><span>Map link</span><input value={form.map_url} placeholder="https://maps…" onChange={set("map_url")} /></label>
            </div>
            <label className="field"><span>Address</span><textarea rows={2} value={form.address} onChange={set("address")} /></label>

            <div className="small-label" style={{ marginTop: 10 }}>Remote access</div>
            <table className="list ra-table">
              <thead><tr><th>Tool</th><th>ID / address</th><th>Notes</th><th /></tr></thead>
              <tbody>
                {form.remote_access.map((r, i) => (
                  <tr key={i}>
                    <td><input value={r.tool} placeholder="AnyDesk / TeamViewer / Router" onChange={(e) => setRa(i, "tool", e.target.value)} /></td>
                    <td><input value={r.address} onChange={(e) => setRa(i, "address", e.target.value)} /></td>
                    <td><input value={r.notes} onChange={(e) => setRa(i, "notes", e.target.value)} /></td>
                    <td><button className="icon-btn" aria-label="Remove" onClick={() => setForm({ ...form, remote_access: form.remote_access.filter((_, j) => j !== i) })}>×</button></td>
                  </tr>
                ))}
              </tbody>
            </table>
            <button className="btn small ghost" onClick={() => setForm({ ...form, remote_access: [...form.remote_access, { tool: "", address: "", notes: "" }] })}>+ Remote access</button>

            <label className="field" style={{ marginTop: 10 }}><span>Notes</span><textarea rows={3} value={form.notes} onChange={set("notes")} /></label>

            {selId !== "new" && (
              <>
                <div className="small-label" style={{ marginTop: 10 }}>Devices at this shed ({shedDevices.length})</div>
                <div className="chip-list">
                  {shedDevices.map((d) => (
                    <button key={d.id} className="pick" onClick={() => onOpenDevice(d.id)}>
                      {d.label} {d.device_type && <span className={`type-badge t-${d.device_type.toLowerCase()}`}>{d.device_type}</span>}
                    </button>
                  ))}
                </div>
              </>
            )}
          </>
        )}
      </section>
    </div>
  );
}
