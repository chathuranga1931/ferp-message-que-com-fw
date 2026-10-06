import { useMemo, useState } from "react";
import type { Harness } from "../types";

/** Wire harness tags (SWH0xx) with a "+" picker. A device can carry several. */
export function HarnessTags({ value, catalog, onChange, disabled }:
  { value: string[]; catalog: Harness[]; onChange: (ids: string[]) => void; disabled?: boolean }) {
  const [adding, setAdding] = useState(false);
  const [q, setQ] = useState("");
  const byId = useMemo(() => new Map(catalog.map((h) => [h.id, h])), [catalog]);

  const options = useMemo(() => {
    const f = q.trim().toLowerCase();
    return catalog
      .filter((h) => !value.includes(h.id) && (h.type || h.description))   // skip reserved, undefined IDs
      .filter((h) => !f || `${h.id} ${h.type} ${h.description}`.toLowerCase().includes(f));
  }, [catalog, value, q]);

  const add = (id: string) => { onChange([...value, id]); setQ(""); setAdding(false); };

  return (
    <div className="harness-tags">
      {value.length === 0 && !adding && <span className="muted small">No cables recorded</span>}
      {value.map((id) => {
        const h = byId.get(id);
        return (
          <span key={id} className={`harness-tag ${h?.retired ? "retired" : ""}`}
                title={h ? `${h.type ? h.type + " — " : ""}${h.description}${h.retired ? " (retired)" : ""}` : "Not in the catalogue"}>
            <b>{id}</b>{h?.type && <span className="muted small">{h.type}</span>}
            {!disabled && <button className="icon-btn" aria-label={`Remove ${id}`} onClick={() => onChange(value.filter((x) => x !== id))}>×</button>}
          </span>
        );
      })}
      {!disabled && !adding && <button className="btn small ghost harness-add" onClick={() => setAdding(true)} title="Add a cable">+</button>}
      {adding && (
        <div className="harness-picker">
          <input autoFocus placeholder="SWH…, type or description" value={q} onChange={(e) => setQ(e.target.value)}
                 onKeyDown={(e) => {
                   if (e.key === "Escape") setAdding(false);
                   if (e.key === "Enter" && options[0]) add(options[0].id);
                 }} />
          <div className="harness-options">
            {options.map((h) => (
              <button key={h.id} className={`harness-option ${h.retired ? "retired" : ""}`} onClick={() => add(h.id)}>
                <b>{h.id}</b> <span className="muted small">{h.type}</span> <span className="small">{h.description}</span>
                {h.retired && <span className="inline-error small"> retired</span>}
              </button>
            ))}
            {options.length === 0 && <span className="muted small">No matching cable</span>}
          </div>
          <button className="btn small ghost" onClick={() => setAdding(false)}>Cancel</button>
        </div>
      )}
    </div>
  );
}
