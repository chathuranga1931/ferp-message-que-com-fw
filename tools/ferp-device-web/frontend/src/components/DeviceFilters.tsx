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

/** Shed / Pump type / Board selects built from the values present in `rows`. */
export function DeviceFilters({ rows, value, onChange }:
  { rows: SiteFields[]; value: SiteFilter; onChange: (f: SiteFilter) => void }) {
  const options = useMemo(() => {
    const out = {} as Record<keyof SiteFilter, { values: string[]; hasNone: boolean }>;
    for (const { key, field } of KEYS) {
      const vals = rows.map((r) => (r[field] ?? "").trim());
      out[key] = { values: [...new Set(vals.filter(Boolean))].sort(), hasNone: vals.some((v) => !v) };
    }
    return out;
  }, [rows]);

  return (
    <div className="device-filters">
      {KEYS.map(({ key, label }) => (
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
