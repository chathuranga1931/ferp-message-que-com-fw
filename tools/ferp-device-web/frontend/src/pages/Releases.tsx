import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { useAuth } from "../auth";
import { compareVersions } from "../components/BundlePicker";
import type { FwRelease, ReleaseBook, TestResult, TestRun } from "../types";
import { fmtDateTime } from "../util";

const RESULT_LABEL: Record<TestResult, string> = { pass: "pass", fail: "fail", issue: "issue", untested: "—", na: "n/a" };
const RESULT_CLASS: Record<TestResult, string> = { pass: "ok", fail: "err", issue: "warn", untested: "", na: "" };

function Result({ r }: { r?: TestResult }) {
  const v = r ?? "untested";
  return <span className={`pill rl-res ${RESULT_CLASS[v]}`} title={v}>{RESULT_LABEL[v]}</span>;
}

/** Latest "released" version per product (newest version number wins). */
function latestPerProduct(releases: FwRelease[]): FwRelease[] {
  const best = new Map<string, FwRelease>();
  for (const r of releases) {
    if (r.status !== "released") continue;
    const cur = best.get(r.product);
    if (!cur || compareVersions(r.version, cur.version) > 0) best.set(r.product, r);
  }
  return [...best.values()].sort((a, b) => a.product.localeCompare(b.product, undefined, { numeric: true }));
}

function counts(run: TestRun) {
  const c: Record<string, number> = {};
  for (const it of run.items) c[it.result] = (c[it.result] ?? 0) + 1;
  return c;
}

export default function Releases() {
  const canEdit = useAuth().can("firmware");
  const [book, setBook] = useState<ReleaseBook | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [product, setProduct] = useState("");
  const [editing, setEditing] = useState(false);

  const load = () => api.releases().then(setBook).catch((e) => setErr(e.message));
  useEffect(() => { load(); }, []);

  const products = useMemo(() => [...new Set((book?.releases ?? []).map((r) => r.product))].sort(), [book]);
  const latest = useMemo(() => latestPerProduct(book?.releases ?? []), [book]);
  const releases = useMemo(() => (book?.releases ?? [])
    .filter((r) => !product || r.product === product)
    .sort((a, b) => b.date.localeCompare(a.date) || compareVersions(b.version, a.version)), [book, product]);
  const tests = useMemo(() => [...(book?.tests ?? [])].sort((a, b) => b.date.localeCompare(a.date)), [book]);
  const types = book?.pump_types ?? [];

  if (!book) return <div className="stack"><section className="card empty">{err ?? "Loading releases…"}</section></div>;

  return (
    <div className="stack releases">
      <section className="card">
        <div className="card-head">
          <h3>Current firmware</h3>
          <div className="row wrap">
            {book.updated && <span className="muted small">updated {fmtDateTime(book.updated)}{book.updated_by ? ` by ${book.updated_by}` : ""}</span>}
            {canEdit && <button className="btn small" onClick={() => setEditing(true)}>Edit data</button>}
          </div>
        </div>
        {err && <div className="inline-error">{err}</div>}
        <div className="rl-latest">
          {latest.map((r) => (
            <div key={r.product} className="rl-tile">
              <span className="rl-prod">{r.product}</span>
              <span className="rl-ver">{r.version}</span>
              <span className="muted small">{[r.board && `board ${r.board}`, r.target, r.date].filter(Boolean).join(" · ")}</span>
            </div>
          ))}
          {latest.length === 0 && <div className="muted">No releases recorded yet.</div>}
        </div>
      </section>

      <section className="card">
        <div className="card-head"><h3>Compatibility</h3><span className="muted small">main firmware × display tap × pump type, from end-to-end tests</span></div>
        <div className="table-wrap">
          <table className="list rl-matrix">
            <thead>
              <tr><th>Main</th><th>Display tap</th><th>Boards</th>{types.map((t) => <th key={t} className="center">{t}</th>)}<th>Tested</th><th>Notes</th></tr>
            </thead>
            <tbody>
              {book.compat.map((row, i) => (
                <tr key={i}>
                  <td><code>{row.main}</code></td>
                  <td><code>{row.dt || "—"}</code></td>
                  <td>{row.boards}</td>
                  {types.map((t) => <td key={t} className="center"><Result r={row.results?.[t]} /></td>)}
                  <td className="nowrap">{row.tested ?? <span className="muted">—</span>}</td>
                  <td className="rl-notes">{row.notes ?? ""}</td>
                </tr>
              ))}
              {book.compat.length === 0 && <tr><td colSpan={types.length + 5} className="muted center">No combinations tested yet.</td></tr>}
            </tbody>
          </table>
        </div>
        <p className="muted small rl-legend"><Result r="pass" /> decoded and the sale reached MQTT · <Result r="fail" /> not working · <Result r="issue" /> works with a known problem · <Result r="untested" /> not tested · <Result r="na" /> not supported</p>
      </section>

      <section className="card">
        <div className="card-head"><h3>Test runs <span className="muted">({tests.length})</span></h3></div>
        <div className="stack">
          {tests.map((run, i) => {
            const c = counts(run);
            return (
              <details key={run.id} className="rl-run" open={i === 0}>
                <summary>
                  <span className="rl-run-date">{run.date}</span>
                  <strong>{run.title}</strong>
                  <code className="small">{run.firmware}</code>
                  <span className="rl-run-counts">
                    {(["pass", "fail", "issue"] as TestResult[]).filter((k) => c[k]).map((k) => (
                      <span key={k} className={`pill ${RESULT_CLASS[k]}`}>{c[k]} {k}</span>
                    ))}
                  </span>
                </summary>
                {run.device && <p className="muted small">Device: {run.device}</p>}
                <div className="table-wrap">
                  <table className="list">
                    <thead><tr><th>Check</th><th>Result</th><th>Detail</th></tr></thead>
                    <tbody>
                      {run.items.map((it, j) => (
                        <tr key={j}><td>{it.name}</td><td><Result r={it.result} /></td><td className="rl-notes">{it.detail ?? ""}</td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </details>
            );
          })}
          {tests.length === 0 && <div className="muted">No test runs recorded yet.</div>}
        </div>
      </section>

      <section className="card">
        <div className="card-head">
          <h3>All releases <span className="muted">({releases.length})</span></h3>
          <select value={product} onChange={(e) => setProduct(e.target.value)} aria-label="Product">
            <option value="">All products</option>
            {products.map((p) => <option key={p} value={p}>{p}</option>)}
          </select>
        </div>
        <div className="table-wrap">
          <table className="list">
            <thead><tr><th>Date</th><th>Product</th><th>Version</th><th>Board</th><th>Target</th><th>Status</th><th>Tag</th><th>Notes</th></tr></thead>
            <tbody>
              {releases.map((r, i) => (
                <tr key={`${r.product}-${r.version}-${i}`} className={r.status === "withdrawn" ? "muted" : ""}>
                  <td className="nowrap">{r.date}</td>
                  <td>{r.product}</td>
                  <td><code>{r.version}</code></td>
                  <td>{r.board ?? <span className="muted">—</span>}</td>
                  <td className="small">{r.target}</td>
                  <td><span className={`pill ${r.status === "released" ? "ok" : r.status === "withdrawn" ? "err" : "warn"}`}>{r.status}</span></td>
                  <td>{r.tag ? <code className="small">{r.tag}</code> : <span className="muted">—</span>}</td>
                  <td className="rl-notes">{r.notes ?? ""}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>

      {editing && <EditDialog book={book} onClose={() => setEditing(false)} onSaved={(b) => { setBook(b); setEditing(false); }} />}
    </div>
  );
}

function EditDialog({ book, onClose, onSaved }: { book: ReleaseBook; onClose: () => void; onSaved: (b: ReleaseBook) => void }) {
  const { updated: _u, updated_by: _b, ...data } = book;
  const [text, setText] = useState(() => JSON.stringify(data, null, 2));
  const [err, setErr] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const save = async () => {
    let parsed: ReleaseBook;
    try { parsed = JSON.parse(text); } catch (e) { setErr(`Not valid JSON: ${(e as Error).message}`); return; }
    setSaving(true); setErr(null);
    try { onSaved(await api.saveReleases(parsed)); } catch (e) { setErr((e as Error).message); } finally { setSaving(false); }
  };
  return (
    <div className="modal-backdrop" onMouseDown={(e) => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="modal" role="dialog" aria-modal="true" aria-label="Edit releases data">
        <div className="modal-head"><h3>Edit releases data</h3><button className="modal-close" onClick={onClose} aria-label="Close">×</button></div>
        <p className="muted small">
          Lists: <code>pump_types</code>, <code>releases</code> (product, target, board, version, date, tag, status released|test|withdrawn, notes),
          {" "}<code>compat</code> (main, dt, boards, results per pump type, tested, notes) and <code>tests</code> (id, date, title, firmware, device, items).
          Results: pass, fail, issue, untested, na.
        </p>
        {err && <div className="inline-error">{err}</div>}
        <textarea id="releases-json" className="rl-json" spellCheck={false} value={text} onChange={(e) => setText(e.target.value)} />
        <div className="row modal-actions">
          <button className="btn primary" disabled={saving} onClick={save}>{saving ? "Saving…" : "Save"}</button>
          <button className="btn" onClick={onClose}>Cancel</button>
        </div>
      </div>
    </div>
  );
}
