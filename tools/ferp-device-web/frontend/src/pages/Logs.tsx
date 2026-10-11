import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import { DeviceFilters, matchesFilter, NO_FILTER, type SiteFilter } from "../components/DeviceFilters";
import { onLogData } from "../store";
import type { Device, LogFile } from "../types";
import { copyText, fmtAgo, fmtBytes, fmtDateTime, topicId, useNow } from "../util";

const CHUNK = 256 * 1024;          // bytes per read
const MAX_CHARS = 4_000_000;       // viewer buffer cap (oldest text dropped while following)
const LS_KEY = "ferp.logs.sel";
const LS_MODE = "ferp.logs.mode";
const AUTO_FOLLOW_S = 15 * 60;     // files written to within this window open in LIVE mode

type Mode = "device" | "shed";
interface Sel { date: string; shed: string; pump: string }

/** ?log=<date>/<shed>/<file> or ?log=@mac/<date>/<mac>/<file> opens that file (shareable link). */
const LINKED = new URLSearchParams(location.search).get("log");
const LINKED_MAC = LINKED?.startsWith("@mac/") ? LINKED.split("/")[2] ?? null : null;
const LS_OPEN_MAC = "ferp.logs.mac";   // set by the Logs buttons elsewhere (Fleet, device pop-up): open this device

/** The device another page asked to open (read once, then cleared). */
function takeOpenMac(): string | null {
  try { const m = localStorage.getItem(LS_OPEN_MAC); localStorage.removeItem(LS_OPEN_MAC); return m || null; }
  catch { return null; }
}

function lsGet(key: string, fallback: string): string {
  try { return localStorage.getItem(key) ?? fallback; } catch { return fallback; }
}
function lsSet(key: string, v: string) {
  try { localStorage.setItem(key, v); } catch { /* ignore */ }
}

function loadSel(): Sel {
  const parts = LINKED && !LINKED_MAC ? LINKED.split("/") : null;
  if (parts?.length === 3) return { date: parts[0], shed: parts[1], pump: "" };
  try { return { date: "", shed: "", pump: "", ...JSON.parse(localStorage.getItem(LS_KEY) ?? "{}") }; }
  catch { return { date: "", shed: "", pump: "" }; }
}

export default function Logs({ devices, onOpenSettings }: { devices: Device[]; onOpenSettings: () => void }) {
  const [mode, setMode] = useState<Mode>(() => (LINKED ? (LINKED_MAC ? "device" : "shed") : (lsGet(LS_MODE, "device") as Mode)));
  const [file, setFile] = useState<LogFile | null>(null);
  const [err, setErr] = useState<string | null>(null);
  useEffect(() => lsSet(LS_MODE, mode), [mode]);
  const switchMode = (m: Mode) => { setMode(m); setFile(null); setErr(null); };
  const notConfigured = err && /configured|key file|authentication|SSH/i.test(err);

  return (
    <div className="logs-grid">
      <section className="card logs-nav">
        <div className="seg">
          <button className={mode === "device" ? "active" : ""} onClick={() => switchMode("device")}>By device</button>
          <button className={mode === "shed" ? "active" : ""} onClick={() => switchMode("shed")}>By date &amp; shed</button>
        </div>
        {err && (
          <div className="inline-error">
            {err}
            {notConfigured && <> — <button className="link-btn" onClick={onOpenSettings}>open Settings → Cloud logs</button></>}
          </div>
        )}
        {mode === "device"
          ? <DeviceNav devices={devices} file={file} onFile={setFile} onError={setErr} />
          : <ShedNav file={file} onFile={setFile} onError={setErr} />}
      </section>
      {file ? <LogViewer key={file.path} file={file} /> : (
        <section className="card empty">
          {mode === "device" ? "Set the filters and press Search, pick a device, then list its days." : "Pick a date, shed and file on the left."}
        </section>
      )}
    </div>
  );
}

// ── by device (dump_logs.py layout) ─────────────────────────────────────────

/** Pump IDs of a device ("P01/P02" in one field counts as two). */
function pumpIds(d: Device | null): string[] {
  if (!d) return [];
  return [d.pump_id_1, d.pump_id_2].flatMap((p) => (p ?? "").split(/[\/,]/)).map((p) => p.trim()).filter(Boolean);
}

interface DeviceRow { mac: string; last_date: string; days: number; device: Device | null; device_type?: string; shed?: string; pump_type?: string; board_version?: string }

const DAYS_SHOWN = 10;             // "List days" shows this many days, ending at the chosen date

/** Local date as YYYY-MM-DD. */
function isoDay(d: Date): string {
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}
/** The `n` days ending at `until`, newest first. */
function dayWindow(until: string, n: number): string[] {
  const [y, m, d] = until.split("-").map(Number);
  return Array.from({ length: n }, (_, i) => isoDay(new Date(y, m - 1, d - i)));
}

interface DeviceQuery { search: string; site: SiteFilter; pump: string; showUnknown: boolean }

function DeviceNav({ devices, file, onFile, onError }:
  { devices: Device[]; file: LogFile | null; onFile: (f: LogFile | null) => void; onError: (e: string | null) => void }) {
  // filters (edited freely) → Search → device list for the filters as they were when Search was pressed
  const [search, setSearch] = useState("");
  const [siteFilter, setSiteFilter] = useState<SiteFilter>(NO_FILTER);
  const [pump, setPump] = useState("");                 // pump ID within the selected shed ("" = all)
  const [showUnknown, setShowUnknown] = useState(true);
  const [applied, setApplied] = useState<DeviceQuery | null>(null);
  const [folders, setFolders] = useState<{ mac: string; last_date: string; days: number }[]>([]);
  const [busy, setBusy] = useState(false);
  // selected device → date (default today) → List days → the DAYS_SHOWN days ending that date
  const [openMac] = useState<string | null>(() => LINKED_MAC ?? takeOpenMac());
  const [mac, setMac] = useState<string>(openMac ?? "");
  const [until, setUntil] = useState(() => (LINKED_MAC ? LINKED?.split("/")[1] : undefined) ?? isoDay(new Date()));
  const [listed, setListed] = useState<{ mac: string; until: string } | null>(null);
  const [files, setFiles] = useState<LogFile[]>([]);
  const [daysBusy, setDaysBusy] = useState(false);
  const [since, setSince] = useState("");

  const runSearch = useCallback(async (q: DeviceQuery, fresh = false) => {
    onError(null); setBusy(true);
    try { setFolders(await api.logDevices(fresh)); setApplied(q); }
    catch (e) { onError((e as Error).message); }
    finally { setBusy(false); }
  }, [onError]);

  const listDays = useCallback(async (m: string, day: string, fresh = false) => {
    onError(null); setDaysBusy(true);
    try {
      const fs = await api.logDeviceFiles(m, fresh, day, DAYS_SHOWN);
      setFiles(fs); setListed({ mac: m, until: day });
      const linked = LINKED && fs.find((f) => f.path === LINKED);
      if (linked) onFile(linked);
    } catch (e) { setFiles([]); onError((e as Error).message); }
    finally { setDaysBusy(false); }
  }, [onError, onFile]);

  // a shared link (?log=@mac/<date>/<mac>/<file>) or another page's Logs button opens straight to that device
  useEffect(() => {
    if (!openMac) return;
    setSearch(openMac);
    runSearch({ search: openMac, site: NO_FILTER, pump: "", showUnknown: true });
    listDays(openMac, until);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // join server folders with the registry by MAC
  const byMac = useMemo(() => {
    const m: Record<string, Device> = {};
    devices.forEach((d) => { const t = topicId(d.mac); if (t) m[t] = d; });
    return m;
  }, [devices]);
  const rows: DeviceRow[] = useMemo(() => folders.map((f) => {
    const d = byMac[f.mac] ?? null;
    return { ...f, device: d, device_type: d?.device_type, shed: d?.shed, pump_type: d?.pump_type, board_version: d?.board_version };
  }), [folders, byMac]);
  // filter choices come from the device list, so they work before the first search
  const filterRows = useMemo(() => devices.map((d) => ({ device_type: d.device_type, shed: d.shed, pump_type: d.pump_type,
    board_version: d.board_version })), [devices]);
  // a real shed is selected (not "all" and not "(not set)") → offer its pump IDs
  const shedSelected = siteFilter.shed !== "" && !siteFilter.shed.startsWith("\u0000");
  const shedPumps = useMemo(() => !shedSelected ? [] :
    [...new Set(devices.filter((d) => (d.shed ?? "") === siteFilter.shed).flatMap((d) => pumpIds(d)))]
      .sort((a, b) => a.localeCompare(b, undefined, { numeric: true })),
  [devices, siteFilter.shed, shedSelected]);
  useEffect(() => { setPump(""); }, [siteFilter.shed]);

  const shown = useMemo(() => {
    if (!applied) return [];
    const q = applied.search.trim().toLowerCase().replace(/[:-]/g, "");
    return rows
      .filter((r) => applied.showUnknown || r.device)
      .filter((r) => matchesFilter(r, applied.site))
      .filter((r) => !applied.pump || pumpIds(r.device).includes(applied.pump))
      .filter((r) => !q || [r.mac, r.device?.label, r.device?.shed, r.device?.pump_id_1, r.device?.pump_id_2, r.device?.notes]
        .some((x) => (x ?? "").toLowerCase().replace(/[:-]/g, "").includes(q)))
      .sort((a, b) => Number(!a.device) - Number(!b.device) || (a.device?.label ?? a.mac).localeCompare(b.device?.label ?? b.mac));
  }, [rows, applied]);
  const cur = rows.find((r) => r.mac === mac);
  const query = (): DeviceQuery => ({ search, site: siteFilter, pump, showUnknown });
  const pickDevice = (m: string) => { setMac(m); setListed(null); setFiles([]); onFile(null); };
  const days = listed ? dayWindow(listed.until, DAYS_SHOWN) : [];
  const filesOn = (day: string) => files.filter((f) => f.date === day);

  return (
    <>
      <div className="field"><span>1 · Filter</span></div>
      <input className="search small" placeholder="Label, MAC, shed, pump…" value={search} onChange={(e) => setSearch(e.target.value)}
             onKeyDown={(e) => { if (e.key === "Enter") runSearch(query()); }} />
      <DeviceFilters rows={filterRows} value={siteFilter} onChange={setSiteFilter} />
      {shedSelected && (
        <>
          <div className="field"><span>Pump</span></div>
          <div className="chip-list">
            <button className={`pick ${!pump ? "active" : ""}`} onClick={() => setPump("")}>All</button>
            {shedPumps.map((p) => (
              <button key={p} className={`pick ${pump === p ? "active" : ""}`} onClick={() => setPump(p)}>{p}</button>
            ))}
            {shedPumps.length === 0 && <span className="muted small">No pump IDs set for this shed's devices.</span>}
          </div>
        </>
      )}
      <label className="check small"><input type="checkbox" checked={showUnknown} onChange={(e) => setShowUnknown(e.target.checked)} />
        Include folders not in the device list</label>
      <div className="row wrap">
        <button className="btn small primary" disabled={busy} onClick={() => runSearch(query())}>{busy ? "Searching…" : "Search"}</button>
        {applied && <button className="btn small ghost" disabled={busy} onClick={() => runSearch(applied, true)} title="Search again, fresh from the server">↻</button>}
      </div>

      {applied && (
        <>
          <div className="field"><span>2 · Device <span className="muted">({shown.length})</span></span></div>
          <div className="file-list device-list">
            {shown.map((r) => (
              <button key={r.mac} className={`file-item ${mac === r.mac ? "active" : ""}`} onClick={() => pickDevice(r.mac)}
                      title={r.device?.notes || r.mac}>
                <span className="dev-cell">
                  <b>{r.device?.label ?? (r.mac === "_invalid" ? "Invalid lines" : "Unknown device")}</b>
                  {r.device?.device_type && <span className={`type-badge t-${r.device.device_type.toLowerCase()}`}>{r.device.device_type}</span>}
                  <span className="muted small"><code>{r.mac}</code>{pumpIds(r.device).length ? ` · ${pumpIds(r.device).join(" / ")}` : ""}{r.device?.shed ? ` · ${r.device.shed}` : ""}</span>
                </span>
                <span className="muted small nowrap" title={`${r.days} day(s) with logs in the scanned period`}>{r.last_date}</span>
              </button>
            ))}
            {folders.length > 0 && shown.length === 0 && <span className="muted small">No devices match these filters.</span>}
            {folders.length === 0 && !busy && <span className="muted small">No device logs in the scanned days (dump_logs.py).</span>}
          </div>
        </>
      )}

      {mac && (
        <>
          <div className="field"><span>3 · Days — {cur?.device?.label ?? mac}</span></div>
          <div className="row wrap">
            <input type="date" className="small-date" value={until} max={isoDay(new Date())}
                   onChange={(e) => setUntil(e.target.value || isoDay(new Date()))} title={`List ${DAYS_SHOWN} days ending on this date`} />
            <button className="btn small primary" disabled={daysBusy} onClick={() => listDays(mac, until)}>
              {daysBusy ? "Listing…" : `List ${DAYS_SHOWN} days`}</button>
            {listed && <button className="btn small ghost" disabled={daysBusy} onClick={() => listDays(listed.mac, listed.until, true)} title="Refresh from the server">↻</button>}
          </div>
          {listed && (
            <div className="file-list">
              {days.map((day) => {
                const fs = filesOn(day);
                if (fs.length === 0) return (
                  <button key={day} className="file-item" disabled title="No log from this device on this day">
                    <span><b>{day}</b></span><span className="muted small">no log</span>
                  </button>
                );
                return fs.map((f) => (
                  <button key={f.path} className={`file-item ${file?.path === f.path ? "active" : ""}`} onClick={() => onFile(f)} title={f.name}>
                    <span><b>{day}</b>{fs.length > 1 ? <span className="muted small"> {f.name}</span> : null}</span>
                    <span className="muted small">{fmtBytes(f.size)}</span>
                  </button>
                ));
              })}
            </div>
          )}
          <div className="row wrap zip-row">
            <a className="btn small" href={api.logDeviceZipUrl(mac, since || undefined)} download>⬇ {since ? `since ${since}` : "all days"} (.zip)</a>
            <input type="date" className="small-date" value={since} onChange={(e) => setSince(e.target.value)} title="Zip only from this day" />
          </div>
        </>
      )}
    </>
  );
}

// ── by date & shed (log.py layout) ───────────────────────────────────────────

function ShedNav({ file, onFile, onError }:
  { file: LogFile | null; onFile: (f: LogFile | null) => void; onError: (e: string | null) => void }) {
  const [dates, setDates] = useState<string[]>([]);
  const [sheds, setSheds] = useState<string[]>([]);
  const [files, setFiles] = useState<LogFile[]>([]);
  const [sel, setSel] = useState<Sel>(loadSel);
  const [busy, setBusy] = useState(false);

  useEffect(() => lsSet(LS_KEY, JSON.stringify(sel)), [sel]);

  const loadDates = useCallback(async (fresh = false) => {
    onError(null); setBusy(true);
    try {
      const d = await api.logDates(fresh);
      setDates(d);
      setSel((s) => (d.includes(s.date) ? s : { date: d[0] ?? "", shed: s.shed, pump: s.pump }));
    } catch (e) { onError((e as Error).message); }
    finally { setBusy(false); }
  }, [onError]);

  useEffect(() => { loadDates(); }, [loadDates]);
  useEffect(() => {
    setSheds([]); setFiles([]);
    if (!sel.date) return;
    api.logSheds(sel.date).then((s) => {
      setSheds(s);
      if (sel.shed && !s.includes(sel.shed)) setSel((x) => ({ ...x, shed: "", pump: "" }));
    }).catch((e) => onError(e.message));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sel.date]);
  useEffect(() => {
    setFiles([]);
    if (!sel.date || !sel.shed) return;
    api.logFiles(sel.date, sel.shed).then((fs) => {
      setFiles(fs);
      const linked = LINKED && fs.find((f) => f.path === LINKED);
      if (linked) onFile(linked);
    }).catch((e) => onError(e.message));
  }, [sel.date, sel.shed, onFile, onError]);

  const refreshFiles = () => {
    if (sel.date && sel.shed) api.logFiles(sel.date, sel.shed, true).then(setFiles).catch((e) => onError(e.message));
    else loadDates(true);
  };

  const pumps = useMemo(() => [...new Set(files.map((f) => f.pump ?? "?"))].sort(), [files]);
  // a preselected pump that has no files on this date → show all
  useEffect(() => { if (files.length && sel.pump && !pumps.includes(sel.pump)) setSel((s) => ({ ...s, pump: "" })); },
    [files, pumps, sel.pump]);
  const shown = files.filter((f) => !sel.pump || (f.pump ?? "?") === sel.pump);

  return (
    <>
      <div className="row">
        <label className="field grow">
          <span>Date</span>
          <select value={sel.date} onChange={(e) => setSel({ date: e.target.value, shed: sel.shed, pump: "" })}>
            {dates.length === 0 && <option value="">—</option>}
            {dates.map((d) => <option key={d} value={d}>{d}</option>)}
          </select>
        </label>
        <button className="btn small ghost align-end" disabled={busy} onClick={refreshFiles} title="Refresh from the server">↻</button>
      </div>
      <div className="field"><span>Shed</span></div>
      <div className="chip-list">
        {sheds.map((s) => (
          <button key={s} className={`pick ${sel.shed === s ? "active" : ""}`} onClick={() => setSel({ ...sel, shed: s, pump: "" })}>{s}</button>
        ))}
        {sel.date && sheds.length === 0 && <span className="muted small">No sheds for this date.</span>}
      </div>
      {sel.shed && (
        <>
          <div className="field"><span>Pump</span></div>
          <div className="chip-list">
            <button className={`pick ${!sel.pump ? "active" : ""}`} onClick={() => setSel({ ...sel, pump: "" })}>All</button>
            {pumps.map((p) => (
              <button key={p} className={`pick ${sel.pump === p ? "active" : ""}`} onClick={() => setSel({ ...sel, pump: p })}>{p}</button>
            ))}
          </div>
          <div className="row wrap zip-row">
            <a className="btn small" href={api.logZipUrl(sel.date, sel.shed)} download>⬇ {sel.shed} {sel.date} (.zip)</a>
            {sel.pump && <a className="btn small" href={api.logZipUrl(sel.date, sel.shed, sel.pump)} download>⬇ {sel.pump} only</a>}
          </div>
          <div className="file-list">
            {shown.map((f) => (
              <button key={f.path} className={`file-item ${file?.path === f.path ? "active" : ""}`} onClick={() => onFile(f)} title={f.name}>
                <span><b>{f.pump ?? "?"}</b> {f.time ?? f.name}</span>
                <span className="muted small">{fmtBytes(f.size)}</span>
              </button>
            ))}
            {files.length > 0 && shown.length === 0 && <span className="muted small">No files for this pump.</span>}
          </div>
        </>
      )}
    </>
  );
}

function LogViewer({ file }: { file: LogFile }) {
  const [copied, setCopied] = useState(false);
  // the followed file can move on to the next day (per-device logs), so track the current one
  const [cur, setCur] = useState({ path: file.path, name: file.name });
  const [lastLine, setLastLine] = useState<number | null>(null);
  const [lastCheck, setLastCheck] = useState<number | null>(null);
  const now = useNow(1000);
  const copyLink = async () => {
    const url = `${location.origin}${location.pathname}?log=${encodeURIComponent(cur.path)}#logs`;
    setCopied(await copyText(url));
    setTimeout(() => setCopied(false), 1500);
  };
  const [text, setText] = useState("");
  const [start, setStart] = useState(0);       // file offset of text[0]
  const [end, setEnd] = useState(0);           // file offset after the last byte shown
  const [size, setSize] = useState(file.size);
  const [follow, setFollow] = useState(() => Date.now() / 1000 - file.mtime < AUTO_FOLLOW_S);
  const [autoScroll, setAutoScroll] = useState(true);
  const [wrap, setWrap] = useState(false);
  const [filter, setFilter] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);
  const boxRef = useRef<HTMLPreElement>(null);
  const endRef = useRef(0);
  endRef.current = end;

  useEffect(() => {
    setLoading(true);
    api.logRead(file.path, { length: CHUNK, tail: true })
      .then((c) => { setText(c.text); setStart(c.offset); setEnd(c.end); setSize(c.size); })
      .catch((e) => setErr(e.message))
      .finally(() => setLoading(false));
  }, [file.path]);

  // live follow
  useEffect(() => {
    if (!follow || loading) return;
    let fid: string | null = null;
    let unsub = () => {};
    let alive = true;
    api.logFollow(cur.path, endRef.current).then((r) => {
      if (!alive) { api.logUnfollow(r.follow_id); return; }
      fid = r.follow_id;
      setLastCheck(Date.now() / 1000);
      unsub = onLogData(fid, (ev) => {
        setLastCheck(Date.now() / 1000);
        if (ev.error) { setErr(ev.error); return; }
        setErr(null);
        if (ev.type === "logs.switch" && ev.path) {
          const day = ev.path.split("/")[1] ?? "";
          setText((t) => `${t}${t.endsWith("\n") || !t ? "" : "\n"}──────── ${day} · ${ev.name} ────────\n`);
          setCur({ path: ev.path, name: ev.name ?? ev.path });
          setEnd(0);
          return;
        }
        if (ev.text) {
          setLastLine(Date.now() / 1000);
          setText((t) => {
            const next = t + ev.text;
            if (next.length <= MAX_CHARS) return next;
            const cut = next.indexOf("\n", next.length - MAX_CHARS);
            setStart((s) => s + new TextEncoder().encode(next.slice(0, cut + 1)).length);
            return next.slice(cut + 1);
          });
          setEnd((e) => e + new TextEncoder().encode(ev.text!).length);
        }
        if (ev.size !== undefined) setSize(ev.size);
      });
    }).catch((e) => { setErr(e.message); setFollow(false); });
    const ka = setInterval(() => { if (fid) api.logKeepalive(fid).catch(() => setFollow(false)); }, 15000);
    return () => { alive = false; clearInterval(ka); unsub(); if (fid) api.logUnfollow(fid).catch(() => undefined); };
    // cur.path changes only via logs.switch, which the running follow already handles
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [follow, loading, file.path]);

  useEffect(() => {
    if (autoScroll) boxRef.current?.scrollTo(0, boxRef.current.scrollHeight);
  }, [text, autoScroll, filter, loading]);

  const loadEarlier = async () => {
    if (start <= 0) return;
    const off = Math.max(0, start - CHUNK);
    try {
      const c = await api.logRead(file.path, { offset: off, length: start - off });
      setAutoScroll(false);
      setText((t) => c.text + t);
      setStart(off);
    } catch (e) { setErr((e as Error).message); }
  };

  const reload = async () => {
    setFollow(false);
    try {
      const c = await api.logRead(file.path, { length: CHUNK, tail: true });
      setText(c.text); setStart(c.offset); setEnd(c.end); setSize(c.size); setErr(null); setAutoScroll(true);
    } catch (e) { setErr((e as Error).message); }
  };

  const lines = useMemo(() => {
    if (!filter.trim()) return null;
    const q = filter.toLowerCase();
    return text.split("\n").filter((l) => l.toLowerCase().includes(q));
  }, [text, filter]);

  return (
    <section className="card log-viewer">
      <div className="card-head">
        <div className="log-title">
          <b>{cur.name}</b>
          <span className="muted small"> {fmtBytes(size)} · modified {fmtDateTime(file.mtime)}
            {start > 0 && ` · showing last ${fmtBytes(end - start)}`}</span>
          {follow && <span className="live-pill">● LIVE</span>}
          {follow && <span className="muted small">
            {lastLine ? `last line ${fmtAgo(lastLine, now)}` : "waiting for new lines…"}
            {lastCheck ? ` · checked ${fmtAgo(lastCheck, now)}` : ""}</span>}
        </div>
        <div className="row wrap">
          <button className={`btn small ${follow ? "danger" : "primary"}`} disabled={loading} onClick={() => setFollow(!follow)}>
            {follow ? "Stop following" : "Follow live"}</button>
          <button className="btn small" disabled={start <= 0} onClick={loadEarlier}>Load earlier</button>
          <button className="btn small ghost" onClick={reload}>Reload</button>
          <a className="btn small" href={api.logDownloadUrl(cur.path)} download>Download</a>
          <button className="btn small ghost" onClick={copyLink} title="Link that opens this log">{copied ? "Copied" : "Copy link"}</button>
        </div>
      </div>
      <div className="row wrap log-tools">
        <input className="search small" placeholder="Show only lines containing…" value={filter} onChange={(e) => setFilter(e.target.value)} />
        {lines && <span className="muted small">{lines.length} matching line(s)</span>}
        <label className="check"><input type="checkbox" checked={autoScroll} onChange={(e) => setAutoScroll(e.target.checked)} /> Auto-scroll</label>
        <label className="check"><input type="checkbox" checked={wrap} onChange={(e) => setWrap(e.target.checked)} /> Wrap</label>
      </div>
      {err && <div className="inline-error">{err}</div>}
      <pre className={`log-text ${wrap ? "wrap" : ""}`} ref={boxRef}
           onScroll={(e) => {
             const el = e.currentTarget;
             const atBottom = el.scrollHeight - el.scrollTop - el.clientHeight < 30;
             if (atBottom !== autoScroll) setAutoScroll(atBottom);
           }}>
        {loading ? "Loading…" : lines ? lines.join("\n") : text}
      </pre>
    </section>
  );
}
