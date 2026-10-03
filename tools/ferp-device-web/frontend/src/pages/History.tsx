import { useCallback, useEffect, useState } from "react";
import { api } from "../api";
import type { AuditEntry, Device, HistoryLine } from "../types";
import { fmtDateTime, topicId } from "../util";

const LEVELS = ["cmd", "resp", "evt", "ota", "info", "warn", "error"];
const ACTIONS = ["", "config", "message", "ota", "snapshot", "device", "firmware", "settings", "mqtt", "favorite"];
const PAGE = 300;

type Tab = "console" | "audit";

/** datetime-local value → epoch seconds */
const toEpoch = (v: string) => (v ? new Date(v).getTime() / 1000 : undefined);

export default function History({ devices }: { devices: Device[] }) {
  const [tab, setTab] = useState<Tab>("console");
  const [deviceId, setDeviceId] = useState("");
  const [levels, setLevels] = useState<Set<string>>(new Set());
  const [action, setAction] = useState("");
  const [q, setQ] = useState("");
  const [since, setSince] = useState("");
  const [until, setUntil] = useState("");
  const [lines, setLines] = useState<HistoryLine[]>([]);
  const [audit, setAudit] = useState<AuditEntry[]>([]);
  const [more, setMore] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);

  const dev = devices.find((d) => d.id === deviceId);
  const filters = useCallback((): Record<string, string | number | undefined> => (tab === "console"
    ? { device: dev ? topicId(dev.uuid || dev.mac) : undefined, level: [...levels].join(",") || undefined,
        q: q || undefined, since: toEpoch(since), until: toEpoch(until) }
    : { device: deviceId || undefined, action: action || undefined, q: q || undefined,
        since: toEpoch(since), until: toEpoch(until) }), [tab, dev, deviceId, levels, action, q, since, until]);

  const load = useCallback(async (append = false) => {
    setBusy(true); setErr(null);
    try {
      if (tab === "console") {
        const before = append && lines.length ? lines[lines.length - 1].id : undefined;
        const r = await api.consoleHistory({ ...filters(), limit: PAGE, before_id: before });
        setLines(append ? [...lines, ...r] : r); setMore(r.length === PAGE);
      } else {
        const before = append && audit.length ? audit[audit.length - 1].id : undefined;
        const r = await api.auditHistory({ ...filters(), limit: PAGE, before_id: before });
        setAudit(append ? [...audit, ...r] : r); setMore(r.length === PAGE);
      }
    } catch (e) { setErr((e as Error).message); }
    finally { setBusy(false); }
  }, [tab, filters, lines, audit]);

  // eslint-disable-next-line react-hooks/exhaustive-deps
  useEffect(() => { load(false); }, [tab]);

  const toggleLevel = (l: string) => { const n = new Set(levels); if (n.has(l)) n.delete(l); else n.add(l); setLevels(n); };

  return (
    <div className="stack">
      <section className="card">
        <div className="card-head">
          <div className="tabs">
            <button className={`tab ${tab === "console" ? "active" : ""}`} onClick={() => setTab("console")}>Console history</button>
            <button className={`tab ${tab === "audit" ? "active" : ""}`} onClick={() => setTab("audit")}>Audit log</button>
          </div>
          <a className="btn small" href={api.exportUrl(tab, filters())} download>Export {tab === "console" ? ".txt" : ".csv"}</a>
        </div>
        <form className="history-filters" onSubmit={(e) => { e.preventDefault(); load(false); }}>
          <select value={deviceId} onChange={(e) => setDeviceId(e.target.value)}>
            <option value="">All devices</option>
            {devices.map((d) => <option key={d.id} value={d.id}>{d.label}</option>)}
          </select>
          {tab === "console" ? (
            <div className="level-toggles">
              {LEVELS.map((l) => (
                <button type="button" key={l} className={`lvl-chip lvl-${l} ${levels.size && !levels.has(l) ? "off" : ""}`}
                        onClick={() => toggleLevel(l)}>{l}</button>
              ))}
            </div>
          ) : (
            <select value={action} onChange={(e) => setAction(e.target.value)}>
              {ACTIONS.map((a) => <option key={a} value={a}>{a ? `${a}.*` : "All actions"}</option>)}
            </select>
          )}
          <input className="search small" placeholder="Contains text…" value={q} onChange={(e) => setQ(e.target.value)} />
          <label className="field inline small-label"><span>From</span><input type="datetime-local" value={since} onChange={(e) => setSince(e.target.value)} /></label>
          <label className="field inline small-label"><span>To</span><input type="datetime-local" value={until} onChange={(e) => setUntil(e.target.value)} /></label>
          <button className="btn small primary" type="submit" disabled={busy}>{busy ? "Loading…" : "Search"}</button>
        </form>
        {err && <div className="inline-error">{err}</div>}

        {tab === "console" ? (
          <div className="console-box tall">
            {lines.map((e) => (
              <div key={e.id} className={`line lvl-${e.level}`}>
                <span className="ts">{fmtDateTime(e.ts)}</span>
                {e.device && <span className="dev">{e.device}</span>}
                <span className="txt">{e.text}</span>
              </div>
            ))}
            {lines.length === 0 && !busy && <div className="muted">No matching lines.</div>}
          </div>
        ) : (
          <div className="table-wrap tall">
            <table className="list">
              <thead><tr><th>Time</th><th>User</th><th>Action</th><th>Device</th><th>Details</th></tr></thead>
              <tbody>
                {audit.map((a) => (
                  <tr key={a.id}>
                    <td>{fmtDateTime(a.ts)}</td>
                    <td className="muted">{a.user}</td>
                    <td><code>{a.action}</code></td>
                    <td>{a.device_label ?? <span className="muted">—</span>}</td>
                    <td className="wrap-cell"><AuditDetail d={a.detail} /></td>
                  </tr>
                ))}
                {audit.length === 0 && !busy && <tr><td colSpan={5} className="muted center">No matching entries.</td></tr>}
              </tbody>
            </table>
          </div>
        )}
        {more && <div className="row center-row"><button className="btn small" disabled={busy} onClick={() => load(true)}>Load older</button></div>}
        <p className="muted small">Newest first. Console history keeps every console line, including lines cleared from the live view.</p>
      </section>
    </div>
  );
}

function AuditDetail({ d }: { d: Record<string, unknown> }) {
  const entries = Object.entries(d);
  if (!entries.length) return <span className="muted">—</span>;
  return (
    <span className="audit-detail">
      {entries.map(([k, v]) => (
        <span key={k}><span className="muted">{k}=</span>{typeof v === "object" ? JSON.stringify(v) : String(v)} </span>
      ))}
    </span>
  );
}
