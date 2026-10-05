import { useMemo } from "react";

/** Site fields a row can be filtered on (registry values; board falls back to the device-reported HW version). */
export interface SiteFields { device_type?: string; shed?: string; pump_type?: string; board_version?: string }
export interface SiteFilter { type: string; shed: string; pumpType: string; board: string }

export const NO_FILTER: SiteFilter = { type: "", shed: "", pumpType: "", board: "" };
const NONE = "\u0000none";          // option value for "(not set)"

const KEYS: { key: keyof SiteFilter; field: keyof SiteFields; label: string }[] = [
  { key: "type", field: "device_type", label: "Type" },
  { key: "shed", field: "shed", label: "Shed" },
  { key: "pumpType", field: "pump_type", label: "Pump type" },
  { key: "board", field: "board_version", label: "Board" },
];

export function matchesFilter(row: SiteFields, f: SiteFilter): boolean {
  return KEYS.every(({ key, field }) => {
    const want = f[key];
    if (!want) return true;
    const have = (row[field] ?? "").trim();
    return want === NONE ? !have : have === want;
  });
}

export function isFiltering(f: SiteFilter): boolean {
  return KEYS.some(({ key }) => f[key] !== "");
}

/** Device types whose devices have no pump type (the Pump type filter is hidden for them). */
const NO_PUMP_TYPE = new Set(["printer"]);

/** Type buttons (All / COM / Printer …) plus Shed / Pump type / Board selects.
 *  The selects only offer values of the devices of the selected type. */
export function DeviceFilters({ rows, value, onChange }:
  { rows: SiteFields[]; value: SiteFilter; onChange: (f: SiteFilter) => void }) {
  const types = useMemo(() => {
    const counts = new Map<string, number>();
    let none = 0;
    for (const r of rows) {
      const t = (r.device_type ?? "").trim();
      if (t) counts.set(t, (counts.get(t) ?? 0) + 1); else none++;
    }
    return { list: [...counts.entries()].sort((a, b) => a[0].localeCompare(b[0])), none };
  }, [rows]);

  // rows of the selected type feed the other selects
  const typed = useMemo(() => rows.filter((r) => matchesFilter(r, { ...NO_FILTER, type: value.type })), [rows, value.type]);
  const options = useMemo(() => {
    const out = {} as Record<keyof SiteFilter, { values: string[]; hasNone: boolean }>;
    for (const { key, field } of KEYS) {
      const vals = typed.map((r) => (r[field] ?? "").trim());
      out[key] = { values: [...new Set(vals.filter(Boolean))].sort(), hasNone: vals.some((v) => !v) };
    }
    return out;
  }, [typed]);

  const hidePumpType = NO_PUMP_TYPE.has(value.type.toLowerCase());
  const setType = (t: string) => {
    const next = { ...value, type: t };
    if (NO_PUMP_TYPE.has(t.toLowerCase())) next.pumpType = "";
    // drop shed / board choices that do not exist for the new type
    const pool = rows.filter((r) => matchesFilter(r, { ...NO_FILTER, type: t }));
    for (const { key, field } of KEYS) {
      if (key === "type" || !next[key] || next[key] === NONE) continue;
      if (!pool.some((r) => (r[field] ?? "").trim() === next[key])) next[key] = "";
    }
    onChange(next);
  };

  return (
    <div className="device-filters">
      <div className="seg type-seg" role="group" aria-label="Filter by type">
        <button className={`btn small ${value.type === "" ? "primary" : "ghost"}`} onClick={() => setType("")}>All ({rows.length})</button>
        {types.list.map(([t, n]) => (
          <button key={t} className={`btn small ${value.type === t ? "primary" : "ghost"}`} onClick={() => setType(t)}>{t} ({n})</button>
        ))}
        {types.none > 0 && (
          <button className={`btn small ${value.type === NONE ? "primary" : "ghost"}`} onClick={() => setType(NONE)}
                  title="Devices without a type">No type ({types.none})</button>
        )}
      </div>
      {KEYS.filter(({ key }) => key !== "type" && !(key === "pumpType" && hidePumpType)).map(({ key, label }) => (
        <select key={key} value={value[key]} onChange={(e) => onChange({ ...value, [key]: e.target.value })}
                className={value[key] ? "active" : ""} aria-label={`Filter by ${label}`}>
          <option value="">{label}: all</option>
          {options[key].values.map((v) => <option key={v} value={v}>{label}: {v}</option>)}
          {options[key].hasNone && <option value={NONE}>{label}: (not set)</option>}
        </select>
      ))}
      {isFiltering(value) && <button className="btn small ghost" onClick={() => onChange(NO_FILTER)}>Clear filters</button>}
    </div>
  );
}

/** "3 selected · 1 hidden by filters [Unselect hidden]" */
export function SelectionNote({ selected, visibleIds, onUnselectHidden, onClear }:
  { selected: Set<string>; visibleIds: Set<string>; onUnselectHidden: () => void; onClear: () => void }) {
  if (selected.size === 0) return null;
  const hidden = [...selected].filter((id) => !visibleIds.has(id)).length;
  return (
    <div className="selection-note">
      <b>{selected.size}</b> selected
      {hidden > 0 && <> · <span className="warn-text">{hidden} hidden by the current filters</span>
        <button className="link-btn" onClick={onUnselectHidden}>unselect hidden</button></>}
      <button className="link-btn" onClick={onClear}>clear selection</button>
    </div>
  );
}
