import { useState } from "react";
import { DEFINITIONS } from "../definitions";

/** Static reference page (README-style). Tables live in src/definitions.ts. */
export default function Definitions() {
  const [filter, setFilter] = useState("");
  const q = filter.trim().toLowerCase();

  return (
    <div className="stack definitions">
      <section className="card">
        <div className="card-head">
          <h3>Definitions</h3>
          <input className="search small" placeholder="Search name or value…" value={filter} onChange={(e) => setFilter(e.target.value)} />
        </div>
        <p className="muted small">Reference mappings used by the devices. Contents: {DEFINITIONS.map((t, i) => (
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
