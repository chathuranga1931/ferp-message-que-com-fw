import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import { onLogData } from "../store";
import type { LogFile } from "../types";
import { copyText, fmtBytes, fmtDateTime } from "../util";

const CHUNK = 256 * 1024;          // bytes per read
const MAX_CHARS = 4_000_000;       // viewer buffer cap (oldest text dropped while following)
const LS_KEY = "ferp.logs.sel";

interface Sel { date: string; shed: string; pump: string }

/** ?log=<date>/<shed>/<file> opens that file (shareable link). */
const LINKED = new URLSearchParams(location.search).get("log");

function loadSel(): Sel {
  const parts = LINKED?.split("/");
  if (parts?.length === 3) return { date: parts[0], shed: parts[1], pump: "" };
  try { return { date: "", shed: "", pump: "", ...JSON.parse(localStorage.getItem(LS_KEY) ?? "{}") }; }
  catch { return { date: "", shed: "", pump: "" }; }
}

export default function Logs({ onOpenSettings }: { onOpenSettings: () => void }) {
  const [dates, setDates] = useState<string[]>([]);
  const [sheds, setSheds] = useState<string[]>([]);
  const [files, setFiles] = useState<LogFile[]>([]);
  const [sel, setSel] = useState<Sel>(loadSel);
  const [file, setFile] = useState<LogFile | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => { try { localStorage.setItem(LS_KEY, JSON.stringify(sel)); } catch { /* ignore */ } }, [sel]);

  const loadDates = useCallback(async (fresh = false) => {
    setErr(null); setBusy(true);
    try {
      const d = await api.logDates(fresh);
      setDates(d);
      setSel((s) => (d.includes(s.date) ? s : { date: d[0] ?? "", shed: s.shed, pump: s.pump }));
    } catch (e) { setErr((e as Error).message); }
    finally { setBusy(false); }
  }, []);

  useEffect(() => { loadDates(); }, [loadDates]);
  useEffect(() => {
    setSheds([]); setFiles([]);
    if (!sel.date) return;
    api.logSheds(sel.date).then((s) => {
      setSheds(s);
      if (sel.shed && !s.includes(sel.shed)) setSel((x) => ({ ...x, shed: "", pump: "" }));
    }).catch((e) => setErr(e.message));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [sel.date]);
  useEffect(() => {
    setFiles([]);
    if (!sel.date || !sel.shed) return;
    api.logFiles(sel.date, sel.shed).then((fs) => {
      setFiles(fs);
      const linked = LINKED && fs.find((f) => f.path === LINKED);
      if (linked) setFile((cur) => cur ?? linked);
    }).catch((e) => setErr(e.message));
  }, [sel.date, sel.shed]);

  const refreshFiles = () => {
    if (sel.date && sel.shed) api.logFiles(sel.date, sel.shed, true).then(setFiles).catch((e) => setErr(e.message));
    else loadDates(true);
  };

  const pumps = useMemo(() => [...new Set(files.map((f) => f.pump ?? "?"))].sort(), [files]);
  // a preselected pump that has no files on this date → show all
  useEffect(() => { if (files.length && sel.pump && !pumps.includes(sel.pump)) setSel((s) => ({ ...s, pump: "" })); },
    [files, pumps, sel.pump]);
  const shown = files.filter((f) => !sel.pump || (f.pump ?? "?") === sel.pump);
  const notConfigured = err && /configured|key file|authentication|SSH/i.test(err);

  return (
    <div className="logs-grid">
      <section className="card logs-nav">
        <div className="card-head">
          <h3>Cloud logs</h3>
          <button className="btn small ghost" disabled={busy} onClick={refreshFiles} title="Refresh from the server">↻</button>
        </div>
        {err && (
          <div className="inline-error">
            {err}
            {notConfigured && <> — <button className="link-btn" onClick={onOpenSettings}>open Settings → Cloud logs</button></>}
          </div>
        )}
        <label className="field">
          <span>Date</span>
          <select value={sel.date} onChange={(e) => setSel({ date: e.target.value, shed: sel.shed, pump: "" })}>
            {dates.length === 0 && <option value="">—</option>}
            {dates.map((d) => <option key={d} value={d}>{d}</option>)}
          </select>
        </label>
        <div className="field"><span>Shed</span></div>
        <div className="chip-list">
          {sheds.map((s) => (
            <button key={s} className={`pick ${sel.shed === s ? "active" : ""}`} onClick={() => setSel({ ...sel, shed: s, pump: "" })}>{s}</button>
          ))}
          {sel.date && sheds.length === 0 && !err && <span className="muted small">No sheds for this date.</span>}
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
                <button key={f.path} className={`file-item ${file?.path === f.path ? "active" : ""}`} onClick={() => setFile(f)} title={f.name}>
                  <span><b>{f.pump ?? "?"}</b> {f.time ?? f.name}</span>
                  <span className="muted small">{fmtBytes(f.size)}</span>
                </button>
              ))}
              {files.length > 0 && shown.length === 0 && <span className="muted small">No files for this pump.</span>}
            </div>
          </>
        )}
      </section>
      {file ? <LogViewer key={file.path} file={file} /> : (
        <section className="card empty">Pick a date, shed and file on the left.</section>
      )}
    </div>
  );
}

function LogViewer({ file }: { file: LogFile }) {
  const [copied, setCopied] = useState(false);
  const copyLink = async () => {
    const url = `${location.origin}${location.pathname}?log=${encodeURIComponent(file.path)}#logs`;
    setCopied(await copyText(url));
    setTimeout(() => setCopied(false), 1500);
  };
  const [text, setText] = useState("");
  const [start, setStart] = useState(0);       // file offset of text[0]
  const [end, setEnd] = useState(0);           // file offset after the last byte shown
  const [size, setSize] = useState(file.size);
  const [follow, setFollow] = useState(false);
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
    api.logFollow(file.path, endRef.current).then((r) => {
      if (!alive) { api.logUnfollow(r.follow_id); return; }
      fid = r.follow_id;
      unsub = onLogData(fid, (ev) => {
        if (ev.error) { setErr(ev.error); return; }
        if (ev.text) {
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
          <b>{file.name}</b>
          <span className="muted small"> {fmtBytes(size)} · modified {fmtDateTime(file.mtime)}
            {start > 0 && ` · showing last ${fmtBytes(end - start)}`}</span>
          {follow && <span className="live-pill">● LIVE</span>}
        </div>
        <div className="row wrap">
          <button className={`btn small ${follow ? "danger" : "primary"}`} disabled={loading} onClick={() => setFollow(!follow)}>
            {follow ? "Stop following" : "Follow live"}</button>
          <button className="btn small" disabled={start <= 0} onClick={loadEarlier}>Load earlier</button>
          <button className="btn small ghost" onClick={reload}>Reload</button>
          <a className="btn small" href={api.logDownloadUrl(file.path)} download>Download</a>
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
