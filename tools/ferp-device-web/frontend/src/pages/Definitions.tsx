import { useEffect, useState } from "react";
import { api } from "../api";
import { DEFINITIONS } from "../definitions";
import type { Harness } from "../types";

/** Definitions → Pump types (firmware display-type table, src/definitions.ts) or Cable types (harness catalog). */
export default function Definitions({ section, onOpenCables }: { section: "pumps" | "cables"; onOpenCables: () => void }) {
  return section === "cables" ? <CableTypes onOpenCables={onOpenCables} /> : <PumpTypes />;
}

function CableTypes({ onOpenCables }: { onOpenCables: () => void }) {
  const [items, setItems] = useState<Harness[]>([]);
  const [err, setErr] = useState<string | null>(null);
  const [filter, setFilter] = useState("");
  useEffect(() => { api.harnesses().then(setItems).catch((e) => setErr(e.message)); }, []);
  const q = filter.trim().toLowerCase();
  const rows = items.filter((h) => !q || [h.id, h.type, h.description].some((x) => (x ?? "").toLowerCase().includes(q)))
    .sort((a, b) => a.id.localeCompare(b.id, undefined, { numeric: true }));
  return (
    <div className="stack definitions">
      <section className="card">
        <div className="card-head">
          <h3>Cable types <span className="muted">({items.length})</span></h3>
          <div className="row wrap">
            <input className="search small" placeholder="Search ID, type, description…" value={filter} onChange={(e) => setFilter(e.target.value)} />
            <button className="btn small" onClick={onOpenCables}>Manage in Cables →</button>
          </div>
        </div>
        <p className="muted small">Wire harnesses (SWH…) fitted with the devices. Stock and which devices use each one are on the Cables page.</p>
        {err && <div className="inline-error">{err}</div>}
        <div className="table-wrap">
          <table className="list def-table">
            <thead><tr><th>ID</th><th>Type</th><th>Description</th><th>In use</th><th>Stock</th></tr></thead>
            <tbody>
              {rows.map((h) => (
                <tr key={h.id} className={h.retired ? "muted" : ""}>
                  <td><code>{h.id}</code>{h.retired && <span className="pill small-pill"> retired</span>}</td>
                  <td>{h.type || <span className="muted">—</span>}</td>
                  <td>{h.description || <span className="muted">—</span>}</td>
                  <td className="num">{h.in_use ?? 0}</td>
                  <td className="num">{h.stock ?? <span className="muted">—</span>}</td>
                </tr>
              ))}
              {rows.length === 0 && <tr><td colSpan={5} className="muted center">{items.length ? "No cable type matches." : "No cable types yet — add them on the Cables page."}</td></tr>}
            </tbody>
          </table>
        </div>
      </section>
    </div>
  );
}

function PumpTypes() {
  const [filter, setFilter] = useState("");
  const q = filter.trim().toLowerCase();

  return (
    <div className="stack definitions">
      <section className="card">
        <div className="card-head">
          <h3>Pump types</h3>
          <input className="search small" placeholder="Search name or value…" value={filter} onChange={(e) => setFilter(e.target.value)} />
        </div>
        <p className="muted small">The pump display protocols the devices read (config key DISPLAY_TYPE). Contents: {DEFINITIONS.map((t, i) => (
          <span key={t.id}>{i > 0 && " · "}<a href={`#def-${t.id}`} onClick={(e) => { e.preventDefault(); document.getElementById(`def-${t.id}`)?.scrollIntoView({ behavior: "smooth" }); }}>{t.title}</a></span>
        ))}</p>
      </section>
      {DEFINITIONS.map((t) => {
        const rows = t.rows.filter((r) => !q || r.name.toLowerCase().includes(q) || String(r.value) === q || (r.note ?? "").toLowerCase().includes(q));
        if (q && rows.length === 0) return null;
        return (
          <section className="card" key={t.id} id={`def-${t.id}`}>
            <div className="card-head"><h3>{t.title}</h3>{t.source && <span className="muted small">{t.source}</span>}</div>
            {t.description && <p className="small">{t.description}</p>}
            <div className="table-wrap">
              <table className="list def-table">
                <thead><tr><th>Value</th><th>Name</th><th>Notes</th></tr></thead>
                <tbody>
                  {rows.map((r) => (
                    <tr key={r.name}>
                      <td><code>{r.value}</code></td>
                      <td><code>{r.name}</code></td>
                      <td className="muted">{r.note ?? ""}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </section>
        );
      })}
    </div>
  );
}
