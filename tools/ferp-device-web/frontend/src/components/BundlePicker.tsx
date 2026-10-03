import { useMemo, useState } from "react";
import type { Firmware } from "../types";
import { fmtBytes, fmtDateTime } from "../util";

/** Filterable list of library bundles with a radio button per row. */
export function BundlePicker({ library, value, onChange, name }:
  { library: Firmware[]; value: string; onChange: (id: string) => void; name: string }) {
  const [target, setTarget] = useState("");
  const [version, setVersion] = useState("");

  const targets = useMemo(() => [...new Set(library.map((b) => b.name))].sort(), [library]);
  const versions = useMemo(() => [...new Set(library.filter((b) => !target || b.name === target).map((b) => b.version))]
    .sort(compareVersions).reverse(), [library, target]);
  const rows = library
    .filter((b) => (!target || b.name === target) && (!version || b.version.includes(version)))
    .sort((a, b) => a.name.localeCompare(b.name) || compareVersions(b.version, a.version));

  return (
    <div className="bundle-picker">
      <div className="row wrap">
        <select value={target} onChange={(e) => { setTarget(e.target.value); setVersion(""); }} className={target ? "active" : ""}>
          <option value="">Target: all</option>
          {targets.map((t) => <option key={t} value={t}>Target: {t}</option>)}
        </select>
        <input className="search small" list={`${name}-versions`} placeholder="Version…" value={version}
               onChange={(e) => setVersion(e.target.value)} />
        <datalist id={`${name}-versions`}>{versions.map((v) => <option key={v} value={v} />)}</datalist>
        <span className="muted small">{rows.length} of {library.length}</span>
      </div>
      <div className="bundle-list">
        {rows.map((b) => (
          <label key={b.id} className={`bundle-row ${value === b.id ? "active" : ""}`}>
            <input type="radio" name={name} checked={value === b.id} onChange={() => onChange(b.id)} />
            <b>{b.name}</b>
            <code>v{b.version}</code>
            <span className="muted small ellipsis">{b.filename}</span>
            <span className="muted small">{fmtDateTime(b.built)} · {fmtBytes(b.size)}</span>
          </label>
        ))}
        {rows.length === 0 && <div className="muted small pad">No bundles match.</div>}
      </div>
    </div>
  );
}

/** Numeric-aware version compare: "1.0.0.140" > "1.0.0.39". */
export function compareVersions(a: string, b: string): number {
  const pa = a.split(/[.\-]/), pb = b.split(/[.\-]/);
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const x = pa[i] ?? "", y = pb[i] ?? "";
    const nx = Number(x), ny = Number(y);
    const c = !Number.isNaN(nx) && !Number.isNaN(ny) ? nx - ny : x.localeCompare(y);
    if (c) return c;
  }
  return 0;
}

/** Display-tap bundle kind by target name: boot → part → fw. */
export function dtKind(target: string): "boot" | "part" | "fw" | null {
  const t = target.toLowerCase();
  if (!t.includes("dt")) return null;
  if (t.includes("boot")) return "boot";
  if (t.includes("part")) return "part";
  if (t.includes("fw") || t.includes("app") || t.includes("firmware")) return "fw";
  return null;
}
