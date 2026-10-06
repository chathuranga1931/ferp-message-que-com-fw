import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import type { Device, Harness } from "../types";

const BLANK: Harness = { id: "", type: "", description: "", retired: false, stock: null };

interface Props { devices: Device[]; onOpenDevice: (id: string) => void }

/** Wire harness catalogue (SWH0xx): definitions, which devices carry each one. Stock comes later. */
export default function Cables({ devices, onOpenDevice }: Props) {
  const [items, setItems] = useState<Harness[]>([]);
  const [edit, setEdit] = useState<Harness | null>(null);
  const [isNew, setIsNew] = useState(false);
  const [filter, setFilter] = useState("");
  const [typeFilter, setTypeFilter] = useState("");
  const [hideEmpty, setHideEmpty] = useState(true);
  const [expanded, setExpanded] = useState<string | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);

  const reload = () => api.harnesses().then(setItems).catch((e) => setMsg({ ok: false, text: e.message }));
  useEffect(() => { reload(); }, [devices]);

  const types = useMemo(() => [...new Set(items.map((h) => h.type).filter(Boolean))].sort(), [items]);
  const f = filter.trim().toLowerCase();
  const shown = items
    .filter((h) => !hideEmpty || h.type || h.description || (h.in_use ?? 0) > 0)
    .filter((h) => !typeFilter || h.type === typeFilter)
    .filter((h) => !f || `${h.id} ${h.type} ${h.description}`.toLowerCase().includes(f));

  const save = async () => {
    if (!edit) return;
    setMsg(null);
    if (isNew && items.some((h) => h.id.toLowerCase() === edit.id.trim().toLowerCase())) {
      setMsg({ ok: false, text: `${edit.id} already exists` }); return;
    }
    try {
      await api.saveHarness({ ...edit, id: edit.id.trim() });
      setEdit(null); reload();
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const remove = async (h: Harness) => {
    try { await api.deleteHarness(h.id); reload(); }
    catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  return (
    <section className="card">
      <div className="card-head">
        <h3>Cables / wire harnesses</h3>
        <div className="row wrap">
          <input className="search small" placeholder="Filter…" value={filter} onChange={(e) => setFilter(e.target.value)} />
          <select value={typeFilter} onChange={(e) => setTypeFilter(e.target.value)}>
            <option value="">Type: all</option>
            {types.map((t) => <option key={t} value={t}>Type: {t}</option>)}
          </select>
          <label className="check small"><input type="checkbox" checked={hideEmpty} onChange={(e) => setHideEmpty(e.target.checked)} /> Hide undefined IDs</label>
          <button className="btn small primary" onClick={() => { setIsNew(true); setEdit({ ...BLANK }); }}>+ New cable</button>
        </div>
      </div>
      {msg && <div className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</div>}
      <div className="muted small">Fit cables to a device on its device page (the + next to Cables). Stock keeping will be added here later.</div>

      {edit && (
        <div className="edit-box">
          <div className="form-grid">
            <label className="field"><span>ID</span><input value={edit.id} disabled={!isNew} placeholder="SWH054" onChange={(e) => setEdit({ ...edit, id: e.target.value })} /></label>
            <label className="field"><span>Type</span>
              <input list="harness-types" value={edit.type} onChange={(e) => setEdit({ ...edit, type: e.target.value })} />
              <datalist id="harness-types">{types.map((t) => <option key={t} value={t} />)}</datalist></label>
            <label className="field"><span>Stock (later)</span>
              <input type="number" value={edit.stock ?? ""} onChange={(e) => setEdit({ ...edit, stock: e.target.value === "" ? null : Number(e.target.value) })} /></label>
          </div>
          <label className="field"><span>Description</span><input value={edit.description} onChange={(e) => setEdit({ ...edit, description: e.target.value })} /></label>
          <label className="check"><input type="checkbox" checked={edit.retired} onChange={(e) => setEdit({ ...edit, retired: e.target.checked })} /> Retired (no longer fitted)</label>
          <div className="row">
            <button className="btn small primary" disabled={!edit.id.trim()} onClick={save}>Save</button>
            <button className="btn small ghost" onClick={() => setEdit(null)}>Cancel</button>
          </div>
        </div>
      )}

      <div className="table-wrap tall">
        <table className="list">
          <thead><tr><th>ID</th><th>Type</th><th>Description</th><th>Devices</th><th>Stock</th><th /></tr></thead>
          <tbody>
            {shown.map((h) => {
              const using = devices.filter((d) => d.harnesses?.includes(h.id));
              return (
                <tr key={h.id} className={h.retired ? "retired-row" : ""}>
                  <td><b>{h.id}</b>{h.retired && <span className="pill err small"> retired</span>}</td>
                  <td>{h.type || <span className="muted">—</span>}</td>
                  <td>{h.description || <span className="muted">not defined yet</span>}</td>
                  <td>
                    {using.length === 0 ? <span className="muted">0</span> : (
                      <button className="link-btn" onClick={() => setExpanded(expanded === h.id ? null : h.id)}>{using.length}</button>
                    )}
                    {expanded === h.id && (
                      <div className="chip-list">
                        {using.map((d) => <button key={d.id} className="pick" onClick={() => onOpenDevice(d.id)}>{d.label}</button>)}
                      </div>
                    )}
                  </td>
                  <td>{h.stock ?? <span className="muted">—</span>}</td>
                  <td className="row-actions">
                    <button className="btn small ghost" onClick={() => { setIsNew(false); setEdit({ ...h }); }}>Edit</button>
                    <button className="btn small ghost" disabled={using.length > 0} title={using.length ? "Fitted to devices" : ""} onClick={() => remove(h)}>Delete</button>
                  </td>
                </tr>
              );
            })}
            {shown.length === 0 && <tr><td colSpan={6} className="muted center">No cables match.</td></tr>}
          </tbody>
        </table>
      </div>
    </section>
  );
}
