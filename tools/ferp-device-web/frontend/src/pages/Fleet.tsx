import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { DeviceFilters, matchesFilter, NO_FILTER, SelectionNote, type SiteFilter } from "../components/DeviceFilters";
import { fleetKey, seedFleet, useLive } from "../store";
import type { Device, FleetRow } from "../types";
import { fmtAgo, fmtDateTime, topicId, useNow } from "../util";

type Status = "online" | "offline" | "never";

/** Filterable site fields of a fleet row (board falls back to the HW version the device reported). */
const siteOf = (r: FleetRow) => ({ device_type: r.device_type, shed: r.shed, pump_type: r.pump_type, board_version: r.board_version || r.info.hw_version });
const STATUS_LABEL: Record<Status, string> = { online: "Online", offline: "Offline", never: "Never seen" };
const LS_VIEW = "ferp.fleet.view";
type View = "table" | "tiles";

function loadView(): View {
  try { return localStorage.getItem(LS_VIEW) === "tiles" ? "tiles" : "table"; } catch { return "table"; }
}

/** Filter state that survives switching tabs (the page unmounts) for this browser session. */
function useSessionState<T>(key: string, fallback: T): [T, (v: T) => void] {
  const [value, setValue] = useState<T>(() => {
    try { const s = sessionStorage.getItem(key); return s === null ? fallback : (JSON.parse(s) as T); } catch { return fallback; }
  });
  useEffect(() => { try { sessionStorage.setItem(key, JSON.stringify(value)); } catch { /* storage unavailable */ } }, [key, value]);
  return [value, setValue];
}

interface Props {
  devices: Device[]; onOpen: (id: string) => void; onOta: (ids: string[]) => void;
  onLogs: (mac: string) => void; onDevicesChanged: () => void;
}

export default function Fleet({ devices, onOpen, onOta, onLogs, onDevicesChanged }: Props) {
  const fleet = useLive((s) => s.fleet);
  const timeout = useLive((s) => s.fleetTimeout);
  const skew = useLive((s) => s.clockSkew);
  const connected = useLive((s) => s.mqtt?.connected ?? false);
  const now = useNow(5000) + skew;
  const [filter, setFilter] = useSessionState("ferp.fleet.filter", "");
  const [statusFilter, setStatusFilter] = useSessionState<"" | Status | "unregistered">("ferp.fleet.status", "");
  const [siteFilter, setSiteFilter] = useSessionState<SiteFilter>("ferp.fleet.site", NO_FILTER);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [view, setView] = useState<View>(loadView);
  // device_id → time the row's own Probe was clicked; cleared when a newer probe result arrives
  const [probing, setProbing] = useState<Record<string, number>>({});
  const changeView = (v: View) => { setView(v); try { localStorage.setItem(LS_VIEW, v); } catch { /* private mode */ } };

  useEffect(() => {
    setProbing((p) => {
      let changed = false;
      const next = { ...p };
      for (const [id, t] of Object.entries(p)) {
        const r = Object.values(fleet).find((x) => x.device_id === id);
        if ((r?.probe && r.probe.ts * 1000 >= t - 1000) || Date.now() - t > 30000) { delete next[id]; changed = true; }
      }
      return changed ? next : p;
    });
  }, [fleet, now]);

  // devices change (add/delete) → refresh the fleet view, which joins registry + presence
  useEffect(() => { api.fleet().then(seedFleet).catch((e) => setMsg({ ok: false, text: e.message })); }, [devices]);

  const statusOf = (r: FleetRow): Status => (!r.last_seen ? "never" : now - r.last_seen <= timeout ? "online" : "offline");

  const rows = useMemo(() => {
    const f = filter.trim().toLowerCase();
    const order: Record<Status, number> = { online: 0, offline: 1, never: 2 };
    return Object.values(fleet)
      .filter((r) => !f || [r.label ?? "", r.topic_id, r.group, r.info.fw_version ?? "", r.shed ?? "", r.pump_id_1 ?? "", r.device_type ?? "",
        r.pump_id_2 ?? "", r.pump_type ?? "", r.board_version ?? ""].some((x) => x.toLowerCase().includes(f)))
      .filter((r) => matchesFilter(siteOf(r), siteFilter))
      .filter((r) => !statusFilter || (statusFilter === "unregistered" ? !r.registered : statusOf(r) === statusFilter))
      .sort((a, b) => Number(b.registered) - Number(a.registered) || order[statusOf(a)] - order[statusOf(b)]
        || (a.label ?? a.topic_id).localeCompare(b.label ?? b.topic_id));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [fleet, filter, statusFilter, siteFilter, now, timeout]);
  const allRows = useMemo(() => Object.values(fleet).map(siteOf), [fleet]);

  const counts = useMemo(() => {
    const c = { online: 0, offline: 0, never: 0, unregistered: 0 };
    Object.values(fleet).forEach((r) => { c[statusOf(r)]++; if (!r.registered) c.unregistered++; });
    return c;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [fleet, now, timeout]);

  const selectable = rows.filter((r) => r.device_id);
  const visibleIds = new Set(selectable.map((r) => r.device_id!));
  const selectedIds = [...selected].filter((id) => devices.some((d) => d.id === id));
  const toggle = (id: string) => { const n = new Set(selected); if (n.has(id)) n.delete(id); else n.add(id); setSelected(n); };
  const allSelected = selectable.length > 0 && selectable.every((r) => selected.has(r.device_id!));
  // header checkbox acts on the visible (filtered) rows only; selections hidden by filters are kept
  const toggleVisible = () => {
    const n = new Set(selected);
    selectable.forEach((r) => (allSelected ? n.delete(r.device_id!) : n.add(r.device_id!)));
    setSelected(n);
  };

  const probe = async (ids?: string[]) => {
    setMsg(null);
    try { const r = await api.probe(ids); setMsg({ ok: true, text: `Probing ${r.probing} device(s)…` }); }
    catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const probeOne = async (id: string) => {
    setProbing((p) => ({ ...p, [id]: Date.now() }));
    try { await api.probe([id]); }
    catch (e) {
      setMsg({ ok: false, text: (e as Error).message });
      setProbing((p) => { const n = { ...p }; delete n[id]; return n; });
    }
  };

  const register = async (r: FleetRow) => {
    // 32 hex chars → provisioned UUID (dashes removed), otherwise treat as MAC
    const isUuid = /^[0-9a-f]{32}$/.test(r.topic_id);
    await api.addDevice({ label: r.topic_id, mac: isUuid ? "" : r.topic_id, uuid: isUuid ? r.topic_id : "",
      group: r.group, ip: "", notes: "Added from Fleet",
      shed: "", pump_id_1: "", pump_id_2: "", sd_card_size: "", board_version: r.info.hw_version ?? "", pump_type: "",
      device_type: "COM" }).catch((e) => setMsg({ ok: false, text: e.message }));
    onDevicesChanged();
  };

  const macOf = (r: FleetRow) => topicId(devices.find((d) => d.id === r.device_id)?.mac ?? "");

  const probeResult = (r: FleetRow) => r.probe ? (
    <span className={r.probe.ok ? "ok-text" : "inline-error"} title={r.probe.error ?? fmtDateTime(r.probe.ts)}>
      {r.probe.ok ? `✓ ${r.probe.rtt_ms} ms` : "✗ no reply"} <span className="muted small">{fmtAgo(r.probe.ts, now)}</span>
    </span>) : <span className="muted">—</span>;

  const actions = (r: FleetRow) => {
    const mac = macOf(r);
    const busy = !!(r.device_id && probing[r.device_id]);
    return (
      <>
        {r.registered && (
          <button className="btn small ghost" disabled={!connected || busy} onClick={() => probeOne(r.device_id!)}
                  title="Read the FW version from this device and time the reply">{busy ? "Probing…" : "Probe"}</button>
        )}
        {mac && <button className="btn small ghost" onClick={() => onLogs(mac)} title="UDP logs of this device (by MAC)">Logs</button>}
        {r.registered
          ? <button className="btn small" onClick={() => onOpen(r.device_id!)}>Open</button>
          : <>
              <button className="btn small" onClick={() => register(r)}>Register</button>
              <button className="btn small ghost" onClick={() => api.forget(r.topic_id)}>Forget</button>
            </>}
      </>
    );
  };

  const statusDot = (st: Status) => <span className={`dot ${st === "online" ? "connected" : st === "offline" ? "disconnected" : ""}`} />;

  return (
    <div className="stack">
      <section className="card">
        <div className="card-head">
          <h3>Fleet</h3>
          <div className="row wrap">
            <div className="seg" role="group" aria-label="View">
              <button className={`btn small ${view === "table" ? "primary" : "ghost"}`} onClick={() => changeView("table")} title="Table view">☰ Table</button>
              <button className={`btn small ${view === "tiles" ? "primary" : "ghost"}`} onClick={() => changeView("tiles")} title="Tile view">▦ Tiles</button>
            </div>
            <input className="search small" placeholder="Filter…" value={filter} onChange={(e) => setFilter(e.target.value)} />
            <select value={statusFilter} onChange={(e) => setStatusFilter(e.target.value as typeof statusFilter)}>
              <option value="">All ({Object.keys(fleet).length})</option>
              <option value="online">Online ({counts.online})</option>
              <option value="offline">Offline ({counts.offline})</option>
              <option value="never">Never seen ({counts.never})</option>
              <option value="unregistered">Not registered ({counts.unregistered})</option>
            </select>
            <button className="btn small" disabled={!connected} onClick={() => probe()} title="Read FW version from every registered device">Probe all</button>
            <button className="btn small" disabled={!connected || selectedIds.length === 0} onClick={() => probe(selectedIds)}>
              Probe selected{selectedIds.length ? ` (${selectedIds.length})` : ""}</button>
            <button className="btn small primary" disabled={selectedIds.length === 0} onClick={() => onOta(selectedIds)}>
              OTA{selectedIds.length ? ` (${selectedIds.length})` : ""}…</button>
          </div>
        </div>
        <DeviceFilters rows={allRows} value={siteFilter} onChange={setSiteFilter} />
        <SelectionNote selected={new Set(selectedIds)} visibleIds={visibleIds}
                       onUnselectHidden={() => setSelected(new Set(selectedIds.filter((id) => visibleIds.has(id))))}
                       onClear={() => setSelected(new Set())} />
        <div className="fleet-summary">
          <span><span className="dot connected" /> {counts.online} online</span>
          <span><span className="dot disconnected" /> {counts.offline} offline</span>
          <span><span className="dot" /> {counts.never} never seen</span>
          <span className="muted">“Online” = heard from in the last {Math.round(timeout / 60)} min</span>
        </div>
        {msg && <div className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</div>}
        {view === "tiles" && (
          <>
            <label className="check small tiles-selectall">
              <input type="checkbox" checked={allSelected} onChange={toggleVisible} /> Select all shown
            </label>
            <div className="fleet-tiles">
              {rows.map((r) => {
                const st = statusOf(r);
                const pumps = [r.pump_id_1, r.pump_id_2].filter(Boolean).join(" / ");
                const board = [r.pump_type, r.board_version || r.info.hw_version].filter(Boolean).join(" · ");
                const sel = !!(r.device_id && selected.has(r.device_id));
                return (
                  <div key={fleetKey(r)} className={`fleet-tile s-${st}${r.registered ? "" : " unregistered"}${sel ? " selected" : ""}`}>
                    <div className="tile-head">
                      {r.device_id && <input type="checkbox" checked={sel} onChange={() => toggle(r.device_id!)} aria-label="Select" />}
                      <div className="tile-title">
                        {r.registered ? <button className="link-btn strong" onClick={() => onOpen(r.device_id!)}>{r.label}</button>
                                      : <span className="muted">not registered</span>}
                        {r.device_type && <span className={`type-badge t-${r.device_type.toLowerCase()}`}>{r.device_type}</span>}
                      </div>
                      <span className="tile-status">{statusDot(st)} {STATUS_LABEL[st]}</span>
                    </div>
                    <div className="muted small"><code>{r.topic_id}</code></div>
                    <dl className="tile-kv">
                      <dt>Shed</dt><dd>{r.shed || <span className="muted">—</span>}</dd>
                      <dt>Pumps</dt><dd>{pumps || <span className="muted">—</span>}</dd>
                      <dt>Board</dt><dd>{board || <span className="muted">—</span>}</dd>
                      <dt>FW</dt><dd><code>{r.info.fw_version ?? "—"}</code></dd>
                      <dt>Seen</dt><dd title={r.last_seen ? fmtDateTime(r.last_seen) : ""}>{fmtAgo(r.last_seen, now)}
                        {r.last_msg && <span className="muted small"> · {r.last_msg.replace(/^Msg/, "")}</span>}</dd>
                      <dt>Probe</dt><dd>{probeResult(r)}</dd>
                    </dl>
                    <div className="tile-actions">{actions(r)}</div>
                  </div>
                );
              })}
              {rows.length === 0 && <div className="muted center">No devices match.</div>}
            </div>
          </>
        )}
        {view === "table" && <div className="table-wrap tall">
          <table className="list fleet">
            <thead>
              <tr>
                <th><input type="checkbox" checked={allSelected} aria-label="Select all"
                           title="Select / unselect the rows shown" onChange={toggleVisible} /></th>
                <th>Status</th><th>Device</th><th>Shed</th><th>Pumps</th><th>Type / board</th><th>FW</th><th>Last seen</th><th>Last message</th><th>Probe</th><th />
              </tr>
            </thead>
            <tbody>
              {rows.map((r) => {
                const st = statusOf(r);
                return (
                  <tr key={fleetKey(r)} className={r.registered ? "" : "unregistered"}>
                    <td>{r.device_id && <input type="checkbox" checked={selected.has(r.device_id)} onChange={() => toggle(r.device_id!)} aria-label="Select" />}</td>
                    <td>{statusDot(st)} {STATUS_LABEL[st]}</td>
                    <td>
                      {r.registered ? <button className="link-btn strong" onClick={() => onOpen(r.device_id!)}>{r.label}</button>
                                    : <span className="muted">not registered</span>}
                      {r.device_type && <span className={`type-badge t-${r.device_type.toLowerCase()}`}>{r.device_type}</span>}
                      <div className="muted small"><code>{r.topic_id}</code></div>
                    </td>
                    <td>{r.shed || <span className="muted">—</span>}</td>
                    <td>{[r.pump_id_1, r.pump_id_2].filter(Boolean).join(" / ") || <span className="muted">—</span>}</td>
                    <td className="small">{[r.pump_type, r.board_version || r.info.hw_version].filter(Boolean).join(" · ") || <span className="muted">—</span>}</td>
                    <td><code>{r.info.fw_version ?? "—"}</code></td>
                    <td title={r.last_seen ? fmtDateTime(r.last_seen) : ""}>{fmtAgo(r.last_seen, now)}</td>
                    <td className="muted">{r.last_msg ? `${r.last_kind} ${r.last_msg.replace(/^Msg/, "")}` : "—"}
                      {r.msg_count > 0 && <span className="small"> · {r.msg_count}</span>}</td>
                    <td>{probeResult(r)}</td>
                    <td className="row-actions">{actions(r)}</td>
                  </tr>
                );
              })}
              {rows.length === 0 && <tr><td colSpan={11} className="muted center">No devices match.</td></tr>}
            </tbody>
          </table>
        </div>}
      </section>

    </div>
  );
}
