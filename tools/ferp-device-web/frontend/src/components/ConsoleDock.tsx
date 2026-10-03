import { useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import { useLive } from "../store";
import type { ConsoleEntry, Device } from "../types";
import { copyText, fmtTime, topicId } from "../util";

const LEVELS: ConsoleEntry["level"][] = ["cmd", "resp", "evt", "ota", "info", "warn", "error"];
const SHOW_MAX = 1500;
const LS = { open: "ferp.console.open", height: "ferp.console.height" };
const MIN_H = 120;

function load<T>(key: string, fallback: T): T {
  try { const v = localStorage.getItem(key); return v === null ? fallback : (JSON.parse(v) as T); } catch { return fallback; }
}
function save(key: string, v: unknown) {
  try { localStorage.setItem(key, JSON.stringify(v)); } catch { /* storage unavailable */ }
}

/**
 * Console docked to the bottom of the window on every page. Shows all activity
 * by default; can be narrowed to the selected device, collapsed, and resized by
 * dragging its top edge.
 */
export function ConsoleDock({ device, devices }: { device: Device | null; devices: Device[] }) {
  const entries = useLive((s) => s.console);
  const [open, setOpen] = useState<boolean>(() => load(LS.open, true));
  const [height, setHeight] = useState<number>(() => load(LS.height, 260));
  const [scope, setScope] = useState<"all" | "device">("all");
  const [hidden, setHidden] = useState<Set<string>>(new Set());
  const [search, setSearch] = useState("");
  const [follow, setFollow] = useState(true);
  const [copied, setCopied] = useState(false);
  const boxRef = useRef<HTMLDivElement>(null);
  const [unseen, setUnseen] = useState(0);
  const lastCount = useRef(entries.length);

  useEffect(() => save(LS.open, open), [open]);
  useEffect(() => save(LS.height, height), [height]);
  // let pages size themselves to the space above the dock (e.g. the log viewer)
  useEffect(() => {
    document.documentElement.style.setProperty("--dock-h", `${open ? height : 36}px`);
  }, [open, height]);

  // count new lines while collapsed
  useEffect(() => {
    if (!open && entries.length > lastCount.current) setUnseen((n) => n + entries.length - lastCount.current);
    lastCount.current = entries.length;
  }, [entries.length, open]);
  useEffect(() => { if (open) setUnseen(0); }, [open]);

  const labels = useMemo(() => {
    const m: Record<string, string> = {};
    devices.forEach((d) => { const t = topicId(d.uuid || d.mac); if (t) m[t] = d.label; });
    return m;
  }, [devices]);

  const devTopic = device ? topicId(device.uuid || device.mac) : null;
  const shown = useMemo(() => {
    const q = search.trim().toLowerCase();
    return entries.filter((e) =>
      !hidden.has(e.level) &&
      (scope === "all" || !devTopic || e.device === null || e.device === devTopic) &&
      (!q || e.text.toLowerCase().includes(q) || (e.device ?? "").includes(q))).slice(-SHOW_MAX);
  }, [entries, hidden, scope, devTopic, search]);

  useEffect(() => {
    if (open && follow) boxRef.current?.scrollTo(0, boxRef.current.scrollHeight);
  }, [shown, follow, open, height]);

  const toggle = (l: string) => {
    const n = new Set(hidden);
    if (n.has(l)) n.delete(l); else n.add(l);
    setHidden(n);
  };

  const copy = async () => {
    const ok = await copyText(shown.map((e) => `[${fmtTime(e.ts)}] [${e.level}]${e.device ? ` ${labels[e.device] ?? e.device}` : ""} ${e.text}`).join("\n"));
    setCopied(ok);
    setTimeout(() => setCopied(false), 1500);
  };

  const startDrag = (ev: React.PointerEvent) => {
    ev.preventDefault();
    const startY = ev.clientY, startH = height;
    const max = window.innerHeight - 140;
    const move = (e: PointerEvent) => setHeight(Math.max(MIN_H, Math.min(max, startH + (startY - e.clientY))));
    const up = () => { window.removeEventListener("pointermove", move); window.removeEventListener("pointerup", up); };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
  };

  return (
    <section className={`console-dock ${open ? "open" : "closed"}`} style={open ? { height } : undefined}>
      {open && <div className="dock-handle" onPointerDown={startDrag} title="Drag to resize" />}
      <div className="dock-bar">
        <button className="dock-toggle" onClick={() => setOpen(!open)} aria-expanded={open}>
          <span className="chev">{open ? "▾" : "▴"}</span> Console
          {!open && unseen > 0 && <span className="badge">{unseen > 999 ? "999+" : unseen}</span>}
        </button>
        {open && (
          <div className="row wrap dock-tools">
            <select value={scope} onChange={(e) => setScope(e.target.value as "all" | "device")}>
              <option value="all">All activity</option>
              <option value="device" disabled={!device}>{device ? `${device.label} + system` : "Selected device (none)"}</option>
            </select>
            <div className="level-toggles">
              {LEVELS.map((l) => (
                <button key={l} className={`lvl-chip lvl-${l} ${hidden.has(l) ? "off" : ""}`} onClick={() => toggle(l)}>{l}</button>
              ))}
            </div>
            <input className="search small" placeholder="Search…" value={search} onChange={(e) => setSearch(e.target.value)} />
            <label className="check"><input type="checkbox" checked={follow} onChange={(e) => setFollow(e.target.checked)} /> Follow</label>
            <button className="btn small" onClick={copy}>{copied ? "Copied" : "Copy"}</button>
            <button className="btn small ghost" onClick={() => api.clearConsole()} title="Clears the live view; history keeps everything">Clear</button>
          </div>
        )}
      </div>
      {open && (
        <div className="dock-body" ref={boxRef}>
          {shown.map((e) => (
            <div key={e.id} className={`line lvl-${e.level}`}>
              <span className="ts">{fmtTime(e.ts)}</span>
              {e.device && <span className="dev" title={e.device}>{labels[e.device] ?? e.device}</span>}
              <span className="txt">{e.text}</span>
            </div>
          ))}
          {shown.length === 0 && <div className="muted">No console output yet.</div>}
        </div>
      )}
    </section>
  );
}
