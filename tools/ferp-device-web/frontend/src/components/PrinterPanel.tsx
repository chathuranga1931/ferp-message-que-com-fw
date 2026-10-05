import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { useLive } from "../store";
import type { ConfigKeyDef, Device } from "../types";
import { cfgFromValues, renderReceipt, renderTotalizer, SAMPLE_JOB, type ReceiptJob, type ReceiptLine } from "../receipt";
import { EMPTY, fmtAgo, useNow } from "../util";

// Wire values of the Printer message group (src/app-messages/printer/printer_types.h)
const PRINTER_CMD = { SAMPLE: 0, RESET_COUNT: 1, INFO: 2, BEEP: 3 } as const;
const JOB_KIND = { RECEIPT: 0, TOTALIZER: 1, SAMPLE: 2 } as const;
const RESULT = ["OK", "Printer not ready", "Write failed", "Invalid job", "Busy"];
const CLOUD_STATE = ["Disabled", "Waiting for internet", "Registering", "Ready", "Registration failed", "No printer queue in cloud"];

interface PrinterStatus {
  transport: string; ready: boolean; last_result: number; print_count: number;
  jobs_ok: number; jobs_failed: number; last_job_id: number; detail: string;
}
interface CloudStatus {
  state: number; notify_subscribed: boolean; last_http: number; polls: number; printed: number;
  failed: number; rejected: number; queue_id: string; notify_topic: string;
}

function nowIso() {
  const d = new Date(), p = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

function Preview({ lines }: { lines: ReceiptLine[] }) {
  return (
    <pre className="receipt">
      {lines.map((l, i) => l.cut
        ? <div key={i} className="receipt-cut">✂ - - - - cut - - - -</div>
        : <div key={i} className={l.big ? "receipt-big" : undefined}>{l.text || " "}</div>)}
    </pre>
  );
}

export function PrinterPanel({ device, keys }: { device: Device; keys: ConfigKeyDef[] }) {
  const connected = useLive((s) => s.mqtt?.connected ?? false);
  const values = useLive((s) => s.config[device.id]) ?? EMPTY;
  const now = useNow(5000);
  const [status, setStatus] = useState<PrinterStatus | null>(null);
  const [cloud, setCloud] = useState<CloudStatus | null>(null);
  const [checked, setChecked] = useState<number | null>(null);
  const [busy, setBusy] = useState<string | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [confirmReset, setConfirmReset] = useState(false);
  const [job, setJob] = useState<ReceiptJob & { kind: number }>(() => ({
    ...SAMPLE_JOB, kind: JOB_KIND.RECEIPT, print_note: "TEST", nozzle_id: "P01", fuel_type: "Petrol 92",
    volume_l: 1, unit_price: 300, total_price: 300, time_stamp: nowIso(), print_time: nowIso(),
  }));
  const [previewOf, setPreviewOf] = useState<"sample" | "test">("sample");

  useEffect(() => { setStatus(null); setCloud(null); setMsg(null); setConfirmReset(false); }, [device.id]);

  const request = (m: string, data: Record<string, unknown>, expect: string, timeout = 10) =>
    api.request(device.id, m, data, expect, timeout);

  const refresh = async () => {
    setBusy("status"); setMsg(null);
    try {
      const p = await request("MsgPrinterGetStatus", {}, "MsgPrinterStatus");
      setStatus(p.data as unknown as PrinterStatus);
      try {
        const c = await request("MsgCloudPrintGetStatus", {}, "MsgCloudPrintStatus");
        setCloud(c.data as unknown as CloudStatus);
      } catch { setCloud(null); }
      setChecked(Date.now() / 1000);
    } catch (e) {
      setMsg({ ok: false, text: String((e as Error).message) });
    } finally { setBusy(null); }
  };

  const command = async (cmd: number, label: string, arg = 0) => {
    setBusy(label); setMsg(null); setConfirmReset(false);
    try {
      const r = await request("MsgPrinterCmd", { cmd, arg }, "MsgPrinterStatus", 30);
      const s = r.data as unknown as PrinterStatus;
      setStatus(s);
      setMsg({ ok: s.last_result === 0, text: `${label}: ${RESULT[s.last_result] ?? s.last_result}` });
    } catch (e) {
      setMsg({ ok: false, text: `${label}: ${(e as Error).message}` });
    } finally { setBusy(null); }
  };

  const printTest = async () => {
    setBusy("print"); setMsg(null);
    try {
      const r = await request("MsgPrintJob", {
        kind: job.kind, job_id: Math.floor(Date.now() / 1000) & 0x7fffffff,
        time: job.time_stamp, print_time: job.print_time, print_note: job.print_note,
        nozzle_id: job.nozzle_id, fuel_type: job.fuel_type, volume_l: job.volume_l,
        unit_price: job.unit_price, total_price: job.total_price, event_id: job.event_id,
        totalizer: job.totalizer,
      }, "MsgPrintResult", 30);
      const d = r.data as { result: number; print_count: number };
      setMsg({ ok: d.result === 0, text: `Test print: ${RESULT[d.result] ?? d.result} (bill count ${d.print_count})` });
      if (status) setStatus({ ...status, print_count: d.print_count });
    } catch (e) {
      setMsg({ ok: false, text: `Test print: ${(e as Error).message}` });
    } finally { setBusy(null); }
  };

  // Preview from the config values read from the device (Config keys panel → Read all)
  const byName = useMemo(() => {
    const m = new Map<string, string>();
    for (const k of keys) { const v = values[String(k.key)]; if (v) m.set(k.name, v.value); }
    return m;
  }, [keys, values]);
  const haveConfig = byName.has("PRN_THEME");
  const cfg = useMemo(() => cfgFromValues((n) => byName.get(n)), [byName]);
  const lines = useMemo(() => {
    const bill = (status?.print_count ?? 0) + 1;
    if (previewOf === "sample") return renderReceipt(cfg, SAMPLE_JOB, bill);
    return job.kind === JOB_KIND.TOTALIZER ? renderTotalizer(cfg, job) : renderReceipt(cfg, job, bill);
  }, [cfg, job, previewOf, status?.print_count]);

  const set = <K extends keyof typeof job>(k: K, v: (typeof job)[K]) => setJob((j) => ({ ...j, [k]: v }));
  const num = (v: string) => (Number.isFinite(Number(v)) ? Number(v) : 0);
  const dis = !connected || busy !== null;

  return (
    <section className="card printer-panel">
      <div className="card-head">
        <h3>Printer</h3>
        <div className="row wrap">
          {checked && <span className="muted small">checked {fmtAgo(checked, now)}</span>}
          <button className="btn small" disabled={dis} onClick={refresh}>{busy === "status" ? "…" : "↻ Status"}</button>
        </div>
      </div>

      {msg && <div className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</div>}

      <div className="printer-status">
        <div>
          <div className="small-label">Printer</div>
          {status ? (
            <>
              <div><span className={`pill ${status.ready ? "ok" : "err"}`}>{status.ready ? "ready" : "not ready"}</span>{" "}
                <code>{status.transport}</code> <span className="muted small">{status.detail}</span></div>
              <div className="small">Bill count <b>{status.print_count}</b> · jobs ok {status.jobs_ok} · failed {status.jobs_failed}
                {status.jobs_failed > 0 && <> · last: {RESULT[status.last_result] ?? status.last_result}</>}</div>
            </>
          ) : <div className="muted small">Press ↻ Status</div>}
        </div>
        <div>
          <div className="small-label">Cloud print</div>
          {cloud ? (
            <>
              <div><span className={`pill ${cloud.state === 3 ? "ok" : cloud.state === 0 ? "" : "warn"}`}>{CLOUD_STATE[cloud.state] ?? cloud.state}</span>
                {cloud.queue_id && <> <code>{cloud.queue_id}</code></>}</div>
              <div className="small">notify {cloud.notify_subscribed ? "subscribed" : "—"} · polls {cloud.polls} · printed {cloud.printed}
                · failed {cloud.failed} · rejected {cloud.rejected}</div>
            </>
          ) : <div className="muted small">{status ? "No reply" : "—"}</div>}
        </div>
      </div>

      <div className="row wrap printer-actions">
        <button className="btn small" disabled={dis} onClick={() => command(PRINTER_CMD.SAMPLE, "Sample")}>Print sample</button>
        <button className="btn small" disabled={dis} onClick={() => command(PRINTER_CMD.INFO, "Info slip")}>Print info slip</button>
        <button className="btn small" disabled={dis} onClick={() => command(PRINTER_CMD.BEEP, "Beep", 1)}>Beep</button>
        {!confirmReset
          ? <button className="btn small ghost" disabled={dis} onClick={() => setConfirmReset(true)}>Reset bill count…</button>
          : <>
              <span className="small">Reset the bill counter to 0?</span>
              <button className="btn small danger" disabled={dis} onClick={() => command(PRINTER_CMD.RESET_COUNT, "Reset count")}>Reset</button>
              <button className="btn small ghost" onClick={() => setConfirmReset(false)}>Cancel</button>
            </>}
      </div>

      <details className="printer-test">
        <summary>Test receipt</summary>
        <div className="form-grid">
          <label className="field"><span>Kind</span>
            <select value={job.kind} onChange={(e) => set("kind", Number(e.target.value))}>
              <option value={JOB_KIND.RECEIPT}>Receipt</option>
              <option value={JOB_KIND.TOTALIZER}>Totalizer</option>
            </select></label>
          <label className="field"><span>Pump</span><input value={job.nozzle_id} onChange={(e) => set("nozzle_id", e.target.value)} /></label>
          <label className="field"><span>Note</span><input value={job.print_note} onChange={(e) => set("print_note", e.target.value)} /></label>
          <label className="field"><span>Fueled time</span><input value={job.time_stamp} onChange={(e) => set("time_stamp", e.target.value)} /></label>
          {job.kind === JOB_KIND.TOTALIZER ? (
            <label className="field"><span>Totalizer</span><input value={job.totalizer} onChange={(e) => set("totalizer", e.target.value)} /></label>
          ) : (
            <>
              <label className="field"><span>Fuel type</span><input value={job.fuel_type} onChange={(e) => set("fuel_type", e.target.value)} /></label>
              <label className="field"><span>Litres</span><input value={job.volume_l} onChange={(e) => setJob((j) => ({ ...j, volume_l: num(e.target.value), total_price: +(num(e.target.value) * j.unit_price).toFixed(2) }))} /></label>
              <label className="field"><span>Unit price</span><input value={job.unit_price} onChange={(e) => setJob((j) => ({ ...j, unit_price: num(e.target.value), total_price: +(j.volume_l * num(e.target.value)).toFixed(2) }))} /></label>
              <label className="field"><span>Total</span><input value={job.total_price} onChange={(e) => set("total_price", num(e.target.value))} /></label>
              <label className="field"><span>Bill no. (empty = counter)</span><input value={job.event_id} onChange={(e) => setJob((j) => ({ ...j, event_id: e.target.value, has_event_id: e.target.value !== "" }))} /></label>
            </>
          )}
        </div>
        <div className="row wrap">
          <button className="btn small primary" disabled={dis} onClick={printTest}>{busy === "print" ? "Printing…" : "Print test receipt"}</button>
          <button className="btn small ghost" onClick={() => setJob((j) => ({ ...j, time_stamp: nowIso(), print_time: nowIso() }))}>Now</button>
        </div>
      </details>

      <div className="card-head printer-preview-head">
        <span className="small-label">Preview — theme {cfg.theme}, {cfg.printer_dots} columns</span>
        <div className="row">
          <button className={`btn small ${previewOf === "sample" ? "primary" : "ghost"}`} onClick={() => setPreviewOf("sample")}>Sample</button>
          <button className={`btn small ${previewOf === "test" ? "primary" : "ghost"}`} onClick={() => setPreviewOf("test")}>Test receipt</button>
        </div>
      </div>
      {!haveConfig && <div className="muted small">Read the config (Config keys → Read all) to preview with this printer's settings; defaults shown.</div>}
      <Preview lines={lines} />
    </section>
  );
}
