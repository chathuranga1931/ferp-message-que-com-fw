import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { api, type EmuReply } from "../api";
import { onBench } from "../store";
import type { BenchItem, BenchLine, BenchMeta, BenchPreset, BenchPumpType, BenchRun, BenchSpec, Device, Firmware } from "../types";
import { downloadText, fmtDateTime, useNow } from "../util";

/** Admin: scripted hardware acceptance test on a bench device (services/bench.py on the server). */

const EMPTY: BenchSpec = {
  device_id: "", device_ip: "", device_port: "", emulator_port: "", main_fw_id: "", dt_fw_id: "",
  pump_types: [], sales: 2, pump_s: 6, pumping_status: false, ota_test: false, ota_alt_fw_id: "", uart_health: true, preset: "",
};
const MAX_LINES = 4000;
const STATE_CLASS: Record<string, string> = { running: "warn", passed: "ok", failed: "err", aborted: "warn", error: "err" };
const RESULT_CLASS: Record<string, string> = { running: "warn", pass: "ok", fail: "err", skip: "" };
type Src = "uart" | "emu" | "mqtt" | "runner";

function dur(a: number, b: number | null, now: number) {
  const s = Math.max(0, Math.round(((b ?? now / 1000) - a)));
  return s >= 3600 ? `${Math.floor(s / 3600)}h ${Math.floor((s % 3600) / 60)}m` : `${Math.floor(s / 60)}m ${s % 60}s`;
}

function parseLog(text: string): BenchLine[] {
  // stored lines: "HH:MM:SS.mmm [src] text"
  return text.split("\n").filter(Boolean).map((l) => {
    const m = /^(\S+) \[([a-z]+)\] (.*)$/.exec(l);
    return m ? { ts: 0, src: m[2], text: `${m[1]}  ${m[3]}` } : { ts: 0, src: "runner", text: l };
  });
}

function fwLabel(f: Firmware) { return `${f.name} v${f.version}${f.notes ? ` — ${f.notes}` : ""}`; }

export default function Bench() {
  const now = useNow(1000);
  const [meta, setMeta] = useState<BenchMeta | null>(null);
  const [devices, setDevices] = useState<Device[]>([]);
  const [firmware, setFirmware] = useState<Firmware[]>([]);
  const [presets, setPresets] = useState<Record<string, BenchPreset>>({});
  const [spec, setSpec] = useState<BenchSpec>(EMPTY);
  const [presetName, setPresetName] = useState("");
  const [run, setRun] = useState<BenchRun | null>(null);
  const [lines, setLines] = useState<BenchLine[]>([]);
  const [show, setShow] = useState<Record<Src, boolean>>({ uart: true, emu: true, mqtt: true, runner: true });
  const [runs, setRuns] = useState<BenchRun[]>([]);
  const [viewing, setViewing] = useState<BenchRun | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [confirmDel, setConfirmDel] = useState("");
  const termRef = useRef<HTMLDivElement>(null);
  const follow = useRef(true);

  const loadRuns = useCallback(() => { api.benchRuns().then(setRuns).catch(() => undefined); }, []);

  useEffect(() => {
    Promise.all([api.benchMeta(), api.devices(), api.firmware(), api.benchPresets(), api.benchSettings()])
      .then(([m, d, f, p, s]) => {
        setMeta(m); setDevices(d); setFirmware(f.filter((x) => x.active)); setPresets(p);
        setSpec((cur) => ({ ...cur, ...s, ...(s.preset && p[s.preset] ? p[s.preset] : {}) }));
        if (m.current) {
          setRun(m.current);
          api.benchLog(m.current.id).then((t) => setLines(parseLog(t).slice(-MAX_LINES))).catch(() => undefined);
        }
      })
      .catch((e) => setMsg({ ok: false, text: e.message }));
    loadRuns();
  }, [loadRuns]);

  useEffect(() => onBench((ev) => {
    if (ev.type === "bench.state") {
      const r = ev.run as BenchRun;
      setRun((cur) => {
        if (!cur || cur.id !== r.id) setLines([]);
        return r;
      });
      if (r.state !== "running") loadRuns();
    } else if (ev.type === "bench.log") {
      setLines((cur) => {
        const next = cur.concat(ev.lines as BenchLine[]);
        return next.length > MAX_LINES ? next.slice(-MAX_LINES) : next;
      });
    }
  }), [loadRuns]);

  useEffect(() => {
    const el = termRef.current;
    if (el && follow.current) el.scrollTop = el.scrollHeight;
  }, [lines, show]);

  const set = <K extends keyof BenchSpec>(k: K, v: BenchSpec[K]) => setSpec((s) => ({ ...s, [k]: v }));
  const toggleType = (code: string) =>
    set("pump_types", spec.pump_types.includes(code) ? spec.pump_types.filter((c) => c !== code) : [...spec.pump_types, code]);

  const device = devices.find((d) => d.id === spec.device_id);
  const mainFw = firmware.filter((f) => /main/i.test(f.name));
  const dtFw = firmware.filter((f) => /dt|esp07/i.test(f.name) && !/boot|part/i.test(f.name));
  const running = run?.state === "running";
  const ports = meta?.ports ?? [];

  const problems = useMemo(() => {
    const p: string[] = [];
    if (!spec.device_id) p.push("select the device");
    if (!spec.device_port) p.push("set the device COM port");
    if (!spec.main_fw_id) p.push("select the main firmware");
    if ((spec.pump_types.length || spec.pumping_status) && !spec.emulator_port) p.push("set the emulator COM port");
    if (spec.emulator_port && spec.emulator_port === spec.device_port) p.push("device and emulator need different COM ports");
    if (spec.ota_test && !spec.ota_alt_fw_id) p.push("select the second (odd/even) bundle for the OTA test");
    if (spec.ota_test && spec.ota_alt_fw_id === spec.main_fw_id) p.push("the OTA test needs a different bundle");
    return p;
  }, [spec]);

  const act = async (fn: () => Promise<unknown>, ok?: string) => {
    setMsg(null);
    try { await fn(); if (ok) setMsg({ ok: true, text: ok }); }
    catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const start = () => act(async () => {
    await api.putBenchSettings(spec);
    setLines([]);
    follow.current = true;
    setRun(await api.startBench(spec));
  });

  const applyPreset = (name: string) => {
    set("preset", name);
    const p = presets[name];
    if (p) setSpec((s) => ({ ...s, preset: name, pump_types: p.pump_types, sales: p.sales, pump_s: p.pump_s,
      pumping_status: p.pumping_status, ota_test: p.ota_test, uart_health: p.uart_health }));
  };
  const savePreset = () => act(async () => {
    const name = presetName.trim();
    const { pump_types, sales, pump_s, pumping_status, ota_test, uart_health } = spec;
    setPresets(await api.saveBenchPreset(name, { pump_types, sales, pump_s, pumping_status, ota_test, uart_health, notes: "" }));
    set("preset", name);
    setPresetName("");
  }, `Preset "${presetName.trim()}" saved`);
  const deletePreset = () => act(async () => {
    setPresets(await api.deleteBenchPreset(spec.preset));
    set("preset", "");
  }, `Preset "${spec.preset}" deleted`);

  const removeRun = (id: string) => {
    if (confirmDel !== id) { setConfirmDel(id); return; }
    setConfirmDel("");
    act(async () => { await api.deleteBenchRun(id); if (viewing?.id === id) setViewing(null); loadRuns(); }, `Report ${id} deleted`);
  };

  const shown = lines.filter((l) => show[(["uart", "emu", "mqtt"].includes(l.src) ? l.src : "runner") as Src]);

  return (
    <div className="stack">
      {msg && <div className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</div>}
      <div className="bench-grid">
        {/* ── setup ─────────────────────────────────────────── */}
        <section className="card">
          <div className="card-head"><h3>Test setup</h3>
            <div className="row">
              <select aria-label="Preset" value={spec.preset} onChange={(e) => applyPreset(e.target.value)}>
                <option value="">— preset —</option>
                {Object.keys(presets).sort().map((n) => <option key={n} value={n}>{n}</option>)}
              </select>
              {spec.preset && <button className="btn small ghost" onClick={deletePreset} disabled={running}>Delete</button>}
            </div>
          </div>
          <div className="form-section">Device</div>
          <div className="form-grid">
            <label className="field">Device
              <select value={spec.device_id} disabled={running} onChange={(e) => {
                const d = devices.find((x) => x.id === e.target.value);
                setSpec((s) => ({ ...s, device_id: e.target.value, device_ip: s.device_ip || d?.ip || "" }));
              }}>
                <option value="">— select —</option>
                {devices.map((d) => <option key={d.id} value={d.id}>{d.label} ({d.mac || d.uuid})</option>)}
              </select>
            </label>
            <label className="field">Local IP address
              <input value={spec.device_ip} disabled={running} placeholder={device?.ip || "e.g. 192.168.8.103"}
                     onChange={(e) => set("device_ip", e.target.value.trim())} />
            </label>
            <label className="field">Device COM port (UART log)
              <input list="bench-ports" value={spec.device_port} disabled={running} placeholder="COM49"
                     onChange={(e) => set("device_port", e.target.value.trim().toUpperCase())} />
            </label>
            <label className="field">Emulator COM port
              <input list="bench-ports" value={spec.emulator_port} disabled={running} placeholder="COM5"
                     onChange={(e) => set("emulator_port", e.target.value.trim().toUpperCase())} />
            </label>
          </div>
          <datalist id="bench-ports">{ports.map((p) => <option key={p.device} value={p.device}>{p.description}</option>)}</datalist>

          <div className="form-section">Firmware the device must run</div>
          <div className="form-grid">
            <label className="field">Main firmware
              <select value={spec.main_fw_id} disabled={running} onChange={(e) => set("main_fw_id", e.target.value)}>
                <option value="">— select —</option>
                {mainFw.map((f) => <option key={f.id} value={f.id}>{fwLabel(f)}</option>)}
              </select>
            </label>
            <label className="field">DT firmware
              <select value={spec.dt_fw_id} disabled={running} onChange={(e) => set("dt_fw_id", e.target.value)}>
                <option value="">— keep what the device has —</option>
                {dtFw.map((f) => <option key={f.id} value={f.id}>{fwLabel(f)}</option>)}
              </select>
            </label>
          </div>
          <p className="muted small">When the device runs other versions, the runner installs these by OTA, reboots twice
            and reads the versions back before testing. A device or board that does not fit stops the run.</p>

          <div className="form-section">Tests</div>
          <label className="check"><input type="checkbox" checked disabled /> Device identity and firmware versions (always)</label>
          <div className="bench-types">
            {(meta?.pump_types ?? []).map((p) => (
              <label key={p.code} className="check" title={`display type ${p.type}`}>
                <input type="checkbox" disabled={running} checked={spec.pump_types.includes(p.code)} onChange={() => toggleType(p.code)} />
                <b>{p.code}</b> <span className="muted">{p.name}</span>
              </label>
            ))}
          </div>
          <div className="row small">
            <button className="btn small ghost" disabled={running} onClick={() => set("pump_types", (meta?.pump_types ?? []).map((p) => p.code))}>All types</button>
            <button className="btn small ghost" disabled={running} onClick={() => set("pump_types", [])}>None</button>
            <label className="field inline">Sales per type
              <input type="number" min={1} max={10} value={spec.sales} disabled={running} onChange={(e) => set("sales", Number(e.target.value))} /></label>
            <label className="field inline">Pumping (s)
              <input type="number" min={2} max={60} value={spec.pump_s} disabled={running} onChange={(e) => set("pump_s", Number(e.target.value))} /></label>
          </div>
          <label className="check"><input type="checkbox" disabled={running} checked={spec.pumping_status}
            onChange={(e) => set("pumping_status", e.target.checked)} /> Pumping status uplink (every 5 s while pumping)</label>
          <label className="check"><input type="checkbox" disabled={running} checked={spec.ota_test}
            onChange={(e) => set("ota_test", e.target.checked)} /> OTA test: main → other release → main</label>
          {spec.ota_test && (
            <label className="field bench-indent">Other (odd/even) main bundle
              <select value={spec.ota_alt_fw_id} disabled={running} onChange={(e) => set("ota_alt_fw_id", e.target.value)}>
                <option value="">— select —</option>
                {mainFw.filter((f) => f.id !== spec.main_fw_id).map((f) => <option key={f.id} value={f.id}>{fwLabel(f)}</option>)}
              </select>
            </label>
          )}
          <label className="check"><input type="checkbox" disabled={running} checked={spec.uart_health}
            onChange={(e) => set("uart_health", e.target.checked)} /> UART log health (no crash or unexpected reset)</label>

          <div className="row bench-actions">
            <input placeholder="Save tests as preset…" value={presetName} onChange={(e) => setPresetName(e.target.value)} />
            <button className="btn small" disabled={!presetName.trim()} onClick={savePreset}>Save preset</button>
            <span className="spacer" />
            {running
              ? <button className="btn danger" onClick={() => act(api.cancelBench, "Cancelling — the current step finishes first")}>Cancel run</button>
              : <button className="btn primary" disabled={problems.length > 0} title={problems.join("; ")} onClick={start}>Run test</button>}
          </div>
          {!running && problems.length > 0 && <p className="muted small">To start: {problems.join(", ")}.</p>}

          <ManualPumping port={spec.emulator_port} device={device} pumpTypes={meta?.pump_types ?? []}
                         busy={running && !!run && run.spec.emulator_port.toUpperCase() === spec.emulator_port.toUpperCase()} />
        </section>

        {/* ── live run ──────────────────────────────────────── */}
        <section className="card bench-live">
          <div className="card-head">
            <h3>{run ? <>Run <span className="mono">{run.tag}</span></> : "No run yet"}</h3>
            {run && <span className={`pill ${STATE_CLASS[run.state]}`}>{run.state}</span>}
          </div>
          {run && (
            <>
              <p className="muted small">{run.device.label} · started {fmtDateTime(run.started)} by {run.user} · {dur(run.started, run.finished, now)}
                {run.versions.main && <> · main <b>{run.versions.main}</b> · DT <b>{run.versions.dt}</b> · board {run.versions.board}</>}</p>
              <Items items={run.items} />
              {run.summary && <p className={run.state === "passed" ? "ok-text" : "inline-error"}>{run.summary}</p>}
            </>
          )}
          <div className="row small bench-filter">
            {(["runner", "uart", "emu", "mqtt"] as Src[]).map((s) => (
              <label key={s} className="check"><input type="checkbox" checked={show[s]} onChange={() => setShow({ ...show, [s]: !show[s] })} />
                <span className={`bench-src-${s}`}>{s === "uart" ? "device UART" : s === "emu" ? "emulator" : s}</span></label>
            ))}
            <span className="spacer" />
            {run && <button className="btn small ghost" onClick={() => api.benchLog(run.id).then((t) => downloadText(`bench-${run.tag}.log`, t, "text/plain"))}>Download log</button>}
          </div>
          <div className="bench-term" ref={termRef}
               onScroll={(e) => { const el = e.currentTarget; follow.current = el.scrollHeight - el.scrollTop - el.clientHeight < 40; }}>
            {shown.length === 0 && <div className="muted">The run's steps, the device UART, emulator replies and fuel events appear here.</div>}
            {shown.map((l, i) => <div key={i} className={`bench-src-${l.src}`}>{l.ts ? `${new Date(l.ts * 1000).toLocaleTimeString()}  ` : ""}{l.text}</div>)}
          </div>
        </section>
      </div>

      {/* ── stored reports ──────────────────────────────────── */}
      <section className="card">
        <div className="card-head"><h3>Test reports <span className="muted">({runs.length})</span></h3>
          <button className="btn small ghost" onClick={loadRuns}>Refresh</button></div>
        <div className="table-wrap">
          <table className="list">
            <thead><tr><th>Tag</th><th>Result</th><th>Device</th><th>Started</th><th>Duration</th><th>By</th><th>Summary</th><th /></tr></thead>
            <tbody>
              {runs.length === 0 && <tr><td colSpan={8} className="muted">No reports yet — completed runs are stored here.</td></tr>}
              {runs.map((r) => (
                <tr key={r.id}>
                  <td className="mono">{r.tag}</td>
                  <td><span className={`pill ${STATE_CLASS[r.state]}`}>{r.state}</span></td>
                  <td>{r.device.label}</td>
                  <td className="small">{fmtDateTime(r.started)}</td>
                  <td className="small">{dur(r.started, r.finished, now)}</td>
                  <td className="small">{r.user}</td>
                  <td className="small">{r.summary}</td>
                  <td className="row-actions">
                    <button className="btn small" onClick={() => api.benchRun(r.id).then(setViewing).catch((e) => setMsg({ ok: false, text: e.message }))}>View</button>
                    <button className="btn small ghost" onClick={() => api.benchLog(r.id).then((t) => downloadText(`bench-${r.tag}.log`, t, "text/plain"))}>Log</button>
                    <button className="btn small ghost" onClick={() => removeRun(r.id)}>{confirmDel === r.id ? "Confirm delete" : "Delete"}</button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>

      {viewing && (
        <section className="card">
          <div className="card-head">
            <h3>Report <span className="mono">{viewing.tag}</span> <span className={`pill ${STATE_CLASS[viewing.state]}`}>{viewing.state}</span></h3>
            <div className="row">
              <button className="btn small ghost" onClick={() => downloadText(`bench-${viewing.tag}.json`, JSON.stringify(viewing, null, 2))}>Download JSON</button>
              <button className="btn small ghost" onClick={() => setViewing(null)}>Close</button>
            </div>
          </div>
          <div className="bench-report-meta small">
            <div><span className="muted">Device</span> {viewing.device.label} ({viewing.device.mac})</div>
            <div><span className="muted">Versions</span> main {viewing.versions.main ?? "?"} · DT {viewing.versions.dt ?? "?"} · board {viewing.versions.board ?? "?"}</div>
            <div><span className="muted">Bundles</span> {viewing.bundles.main?.version}{viewing.bundles.dt ? ` · DT ${viewing.bundles.dt.version}` : ""}{viewing.bundles.alt ? ` · OTA alt ${viewing.bundles.alt.version}` : ""}</div>
            <div><span className="muted">Ports</span> device {viewing.spec.device_port} · emulator {viewing.spec.emulator_port || "—"} · IP {viewing.spec.device_ip || "—"}</div>
            <div><span className="muted">Run</span> {fmtDateTime(viewing.started)} by {viewing.user}{viewing.preset ? ` · preset ${viewing.preset}` : ""} · {dur(viewing.started, viewing.finished, now)}</div>
          </div>
          <Items items={viewing.items} />
          <p className={viewing.state === "passed" ? "ok-text" : "inline-error"}>{viewing.summary}</p>
        </section>
      )}
    </div>
  );
}

function Items({ items }: { items: BenchItem[] }) {
  if (!items.length) return null;
  return (
    <table className="list bench-items">
      <tbody>
        {items.map((i) => (
          <tr key={i.id}>
            <td><span className={`pill ${RESULT_CLASS[i.result]}`}>{i.result}</span></td>
            <td><b>{i.name}</b>{i.detail && <div className="small muted bench-detail">{i.detail}</div>}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

// ── Manual pumping: drive the Mega display emulator by hand ──────────────────

interface EmuLog { ts: number; cmd: string; reply: string[]; ok: boolean }

function parseStatus(lines: string[]): Record<string, string> | null {
  const l = lines.find((x) => x.startsWith("OK STATUS"));
  if (!l) return null;
  const out: Record<string, string> = {};
  for (const m of l.matchAll(/(\w+)=(\S+)/g)) out[m[1]] = m[2];
  return out;
}

function ManualPumping({ port, device, pumpTypes, busy }:
  { port: string; device?: Device; pumpTypes: BenchPumpType[]; busy: boolean }) {
  const [mode, setMode] = useState("CEN6");
  const [alsoDevice, setAlsoDevice] = useState(false);
  const [price, setPrice] = useState("");
  const [volume, setVolume] = useState("");
  const [step, setStep] = useState("");
  const [saleSec, setSaleSec] = useState(6);
  const [working, setWorking] = useState("");
  const [log, setLog] = useState<EmuLog[]>([]);
  const [status, setStatus] = useState<Record<string, string> | null>(null);
  const [err, setErr] = useState("");
  const pt = pumpTypes.find((p) => p.code === mode);
  const off = !port || busy || !!working;

  const record = (rs: EmuReply[]) => {
    const now = Date.now() / 1000;
    setLog((cur) => [...rs.map((r) => ({ ts: now, ...r })).reverse(), ...cur].slice(0, 60));
    for (const r of rs) { const st = parseStatus(r.reply); if (st) setStatus(st); }
  };
  const note = (cmd: string, reply: string[], ok: boolean) =>
    setLog((cur) => [{ ts: Date.now() / 1000, cmd, reply, ok }, ...cur].slice(0, 60));

  const send = async (cmd: string, label = cmd) => {
    setErr(""); setWorking(label);
    try { record([await api.emulatorCmd(port, cmd)]); }
    catch (e) { setErr((e as Error).message); }
    finally { setWorking(""); }
  };

  const auto = async () => {
    setErr(""); setWorking("Auto");
    try { record([await api.emulatorCmd(port, "NOZZLE AUTO"), await api.emulatorCmd(port, "PUMP AUTO")]); }
    catch (e) { setErr((e as Error).message); }
    finally { setWorking(""); }
  };

  const setPumpType = async () => {
    setErr(""); setWorking("Set pump type");
    try {
      record([await api.emulatorCmd(port, `MODE ${mode}`)]);
      if (pt?.price) record([await api.emulatorCmd(port, `PRICE ${pt.price}`)]);
      if (alsoDevice && device && pt) {
        const r = await api.setDeviceType(device.id, pt.type, true);
        note(`device ${device.label}: display type ${pt.type}`,
          [`config ${r.verified ? "written and verified" : "written (not verified)"}`, "device rebooting to apply it"], r.verified);
      }
      record([await api.emulatorCmd(port, "STATUS")]);
    } catch (e) { setErr((e as Error).message); }
    finally { setWorking(""); }
  };

  const sale = async () => {
    setErr(""); setWorking("Sale");
    try {
      const r = await api.emulatorSale(port, saleSec, price ? Number(price) : null);
      record(r.steps);
      if (r.sale) note("sale result", [`${r.sale.vol_l.toFixed(3)} L × ${r.sale.price.toFixed(2)} = ${r.sale.total.toFixed(2)}`], true);
    } catch (e) { setErr((e as Error).message); }
    finally { setWorking(""); }
  };

  const btn = (cmd: string, label: string, cls = "") =>
    <button key={cmd} className={`btn small ${cls}`} disabled={off} onClick={() => send(cmd, label)}>{label}</button>;
  const digits = (v: string) => v.replace(/\D/g, "");

  return (
    <div className="manual-pump">
      <div className="form-section">Manual pumping</div>
      <p className="muted small">Drive the display emulator on {port || "the emulator COM port"} by hand.
        {busy && <b> The running test uses this port — wait or cancel it.</b>}
        {!port && " Set the emulator COM port above first."}</p>

      <div className="row wrap">
        <label className="field inline">Pump type
          <select value={mode} disabled={off} onChange={(e) => setMode(e.target.value)}>
            {pumpTypes.map((p) => <option key={p.code} value={p.code}>{p.code} — {p.name}</option>)}
          </select></label>
        <label className="check" title={device ? `Writes display type ${pt?.type} to ${device.label} and reboots it` : "Select the device above"}>
          <input type="checkbox" checked={alsoDevice} disabled={off || !device} onChange={(e) => setAlsoDevice(e.target.checked)} />
          also set the device&apos;s display type (reboots it)</label>
        <button className="btn small primary" disabled={off} onClick={setPumpType}>Set pump type</button>
      </div>

      <div className="row wrap">
        {btn("NOZZLE 1", "Nozzle up")}
        {btn("PUMP 1", "Pump on")}
        {btn("PUMP 0", "Pump off")}
        {btn("NOZZLE 0", "Nozzle down")}
        <button className="btn small ghost" disabled={off} title="Give control back to the switch and button on the emulator" onClick={auto}>Auto (front panel)</button>
        {btn("STATUS", "Status", "ghost")}
        {btn("PING", "Ping", "ghost")}
      </div>

      <div className="row wrap">
        <label className="field inline">Unit price ×100
          <input value={price} disabled={off} placeholder={pt?.price ? String(pt.price) : "mode default"} inputMode="numeric"
                 onChange={(e) => setPrice(digits(e.target.value))} /></label>
        <button className="btn small" disabled={off || !price} onClick={() => send(`PRICE ${price}`)}>Set price</button>
        <label className="field inline">Volume (mL)
          <input value={volume} disabled={off} inputMode="numeric" onChange={(e) => setVolume(digits(e.target.value))} /></label>
        <button className="btn small" disabled={off || !volume} onClick={() => send(`VOLUME ${volume}`)}>Set volume</button>
        <label className="field inline">Step (mL)
          <input value={step} disabled={off} placeholder="125" inputMode="numeric" onChange={(e) => setStep(digits(e.target.value))} /></label>
        <button className="btn small" disabled={off || !step} onClick={() => send(`STEP ${step}`)}>Set step</button>
      </div>

      <div className="row wrap">
        <label className="field inline">Pump for (s)
          <input type="number" min={1} max={120} value={saleSec} disabled={off} onChange={(e) => setSaleSec(Number(e.target.value))} /></label>
        <button className="btn small primary" disabled={off} title="Nozzle up, pump, stop, read the totals, nozzle down" onClick={sale}>Run one sale</button>
        {working && <span className="muted small">{working}…</span>}
      </div>

      {err && <div className="inline-error">{err}</div>}
      {status && (
        <div className="manual-status small">
          {["mode", "nozzle", "pump", "vol_ml", "price", "total", "step_ml"].filter((k) => status[k] !== undefined).map((k) => (
            <span key={k} className="pill"><span className="muted">{k}</span>{" "}
              {k === "nozzle" ? (status[k] === "1" ? "up" : "down") : k === "pump" ? (status[k] === "1" ? "on" : "off") : status[k]}</span>
          ))}
        </div>
      )}
      {log.length > 0 && (
        <div className="manual-log">
          {log.map((l, i) => (
            <div key={i}><span className="muted">{new Date(l.ts * 1000).toLocaleTimeString()}</span>{" "}
              <b className={l.ok ? "" : "lvl-error"}>{l.cmd}</b>{l.reply.length ? ` → ${l.reply.join(" | ")}` : " → (no reply)"}</div>
          ))}
        </div>
      )}
    </div>
  );
}
