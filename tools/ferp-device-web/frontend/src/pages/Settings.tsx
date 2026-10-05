import { useEffect, useState } from "react";
import { api } from "../api";
import type { AppConfig, Catalog } from "../types";
import { fmtDateTime } from "../util";
import { useTheme, type ThemePref } from "../theme";
import { API_VERSION } from "../version";

const THEMES: { value: ThemePref; label: string; hint: string }[] = [
  { value: "system", label: "System", hint: "follow this computer's setting" },
  { value: "light", label: "Light", hint: "" },
  { value: "dark", label: "Dark", hint: "" },
];

export default function Settings({ catalog, onReloadCatalog }: { catalog: Catalog | null; onReloadCatalog: () => void }) {
  const [saved, setSaved] = useState<AppConfig | null>(null);
  const [cfg, setCfg] = useState<AppConfig | null>(null);
  const [me, setMe] = useState<{ user: string; auth_mode: string } | null>(null);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [newBroker, setNewBroker] = useState("");
  const [theme, setTheme] = useTheme();
  const [health, setHealth] = useState<{ api_version: number; started: number } | null>(null);
  const [restart, setRestart] = useState<"idle" | "confirm" | "waiting" | "done" | "failed">("idle");
  const [logTest, setLogTest] = useState<{ ok: boolean; text: string } | null>(null);

  useEffect(() => {
    api.getConfig().then((c) => { setSaved(c); setCfg(c); }).catch((e) => setMsg({ ok: false, text: e.message }));
    api.me().then(setMe).catch(() => undefined);
    api.health().then(setHealth).catch(() => undefined);
  }, []);

  const doRestart = async () => {
    if (restart !== "confirm") { setRestart("confirm"); return; }
    const before = health?.started ?? 0;
    setRestart("waiting");
    try { await api.restartServer(); } catch { /* connection drops as it exits */ }
    for (let i = 0; i < 45; i++) {          // WinSW restarts it after ~10 s
      await new Promise((r) => setTimeout(r, 2000));
      try {
        const h = await api.health();
        if (h.started > before) { setHealth(h); setRestart("done"); setTimeout(() => location.reload(), 800); return; }
      } catch { /* still down */ }
    }
    setRestart("failed");
  };

  if (!cfg || !saved) return <section className="card">{msg ? <div className="inline-error">{msg.text}</div> : "Loading…"}</section>;

  const dirty = JSON.stringify(cfg) !== JSON.stringify(saved);
  const upd = <S extends keyof AppConfig>(section: S, patch: Partial<AppConfig[S]>) =>
    setCfg({ ...cfg, [section]: { ...cfg[section], ...patch } });
  const num = (v: string) => (v === "" ? 0 : Number(v));

  const save = async () => {
    setMsg(null);
    try {
      const r = await api.putConfig(cfg);
      setSaved(r.config); setCfg(r.config);
      setMsg({ ok: true, text: r.reconnected ? "Saved — MQTT is reconnecting with the new settings." : "Saved." });
    } catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const addBroker = () => {
    const b = newBroker.trim();
    if (b && !cfg.mqtt.brokers.includes(b)) upd("mqtt", { brokers: [...cfg.mqtt.brokers, b] });
    setNewBroker("");
  };

  return (
    <div className="stack settings">
      <section className="card">
        <div className="card-head">
          <h3>Appearance</h3>
          <span className="muted small">Saved in this browser only, applies immediately</span>
        </div>
        <div className="theme-choice" role="radiogroup" aria-label="Theme">
          {THEMES.map((t) => (
            <label key={t.value} className={`theme-option ${theme === t.value ? "active" : ""}`}>
              <input type="radio" name="theme" checked={theme === t.value} onChange={() => setTheme(t.value)} />
              <span className={`theme-swatch ${t.value}`} />
              <span>{t.label}{t.hint && <span className="muted small"> — {t.hint}</span>}</span>
            </label>
          ))}
        </div>
      </section>

      <section className="card">
        <div className="card-head">
          <h3>MQTT broker</h3>
          <span className="muted small">Saved to <code>backend/data/app-config.json</code></span>
        </div>
        <div className="form-grid">
          <label className="field">
            <span>Host</span>
            <input list="brokers" value={cfg.mqtt.host} onChange={(e) => upd("mqtt", { host: e.target.value })} />
            <datalist id="brokers">{cfg.mqtt.brokers.map((b) => <option key={b} value={b} />)}</datalist>
          </label>
          <label className="field"><span>Port</span>
            <input type="number" min={1} max={65535} value={cfg.mqtt.port} onChange={(e) => upd("mqtt", { port: num(e.target.value) })} /></label>
          <label className="field"><span>Username</span>
            <input value={cfg.mqtt.username} autoComplete="off" onChange={(e) => upd("mqtt", { username: e.target.value })} /></label>
          <label className="field"><span>Password</span>
            <input type="password" value={cfg.mqtt.password} autoComplete="new-password" onChange={(e) => upd("mqtt", { password: e.target.value })} /></label>
          <label className="field"><span>Client id (blank = auto)</span>
            <input value={cfg.mqtt.client_id} onChange={(e) => upd("mqtt", { client_id: e.target.value })} /></label>
          <label className="field"><span>Keep-alive (s)</span>
            <input type="number" min={5} value={cfg.mqtt.keepalive} onChange={(e) => upd("mqtt", { keepalive: num(e.target.value) })} /></label>
          <label className="field"><span>Device type (topic segment)</span>
            <input value={cfg.mqtt.dev_type} onChange={(e) => upd("mqtt", { dev_type: e.target.value })} /></label>
          <label className="check field-check">
            <input type="checkbox" checked={cfg.mqtt.auto_connect} onChange={(e) => upd("mqtt", { auto_connect: e.target.checked })} />
            Connect automatically when the service starts
          </label>
        </div>
        <div className="presets">
          <span className="muted small">Broker presets:</span>
          {cfg.mqtt.brokers.map((b) => (
            <span key={b} className="preset">
              <button className="link-btn" onClick={() => upd("mqtt", { host: b })}>{b}</button>
              <button className="icon-btn" aria-label={`Remove ${b}`} onClick={() => upd("mqtt", { brokers: cfg.mqtt.brokers.filter((x) => x !== b) })}>×</button>
            </span>
          ))}
          <input className="search small" placeholder="add host…" value={newBroker} onChange={(e) => setNewBroker(e.target.value)}
                 onKeyDown={(e) => e.key === "Enter" && addBroker()} />
          <button className="btn small" onClick={addBroker}>Add</button>
        </div>
      </section>

      <section className="card">
        <div className="card-head"><h3>Device operations</h3></div>
        <div className="form-grid">
          <label className="field"><span>Response timeout (s)</span>
            <input type="number" step="0.5" min={0.5} value={cfg.device.response_timeout_s}
                   onChange={(e) => upd("device", { response_timeout_s: num(e.target.value) })} /></label>
          <label className="field wide"><span>Bundle folders to import from (one per line; empty = repo releases/)</span>
            <textarea rows={2} value={cfg.ota.bundle_dirs.join("\n")}
                      onChange={(e) => upd("ota", { bundle_dirs: e.target.value.split("\n").map((x) => x.trim()).filter(Boolean) })} /></label>
          <label className="field wide"><span>Bundle targets per device type — one type per line, e.g. <code>Printer: *printer*, *prn*</code> (a type without patterns accepts any bundle)</span>
            <textarea rows={3} value={Object.entries(cfg.ota.type_targets ?? {}).map(([t, p]) => `${t}: ${p.join(", ")}`).join("\n")}
                      onChange={(e) => upd("ota", { type_targets: Object.fromEntries(e.target.value.split("\n")
                        .map((l) => l.split(":")).filter((x) => x.length >= 2 && x[0].trim())
                        .map(([t, ...rest]) => [t.trim(), rest.join(":").split(",").map((p) => p.trim()).filter(Boolean)])) })} /></label>
          <label className="field"><span>Default OTA chunk size</span>
            <select value={cfg.ota.chunk_size} onChange={(e) => upd("ota", { chunk_size: Number(e.target.value) })}>
              {[1024, 2048, 4096, 8192].map((c) => <option key={c} value={c}>{c}</option>)}
            </select></label>
          <label className="field"><span>Console buffer (lines)</span>
            <input type="number" min={100} value={cfg.console.buffer_lines} onChange={(e) => upd("console", { buffer_lines: num(e.target.value) })} /></label>
          <label className="check field-check">
            <input type="checkbox" checked={cfg.device.verify_writes} onChange={(e) => upd("device", { verify_writes: e.target.checked })} />
            Verify config writes by reading each key back
          </label>
        </div>
      </section>

      <section className="card">
        <div className="card-head"><h3>Fleet</h3></div>
        <div className="form-grid">
          <label className="field"><span>Online if heard from within (s)</span>
            <input type="number" min={10} value={cfg.fleet.online_timeout_s} onChange={(e) => upd("fleet", { online_timeout_s: num(e.target.value) })} /></label>
          <label className="field"><span>Probe timeout (s)</span>
            <input type="number" step="0.5" min={0.5} value={cfg.fleet.probe_timeout_s} onChange={(e) => upd("fleet", { probe_timeout_s: num(e.target.value) })} /></label>
          <label className="field"><span>Auto-probe every (s, 0 = off)</span>
            <input type="number" min={0} value={cfg.fleet.auto_probe_interval_s} onChange={(e) => upd("fleet", { auto_probe_interval_s: num(e.target.value) })} /></label>
        </div>
        <p className="muted small">Auto-probe sends a device-info read to every registered device on that schedule. Leave it off to send nothing unless you ask.</p>
      </section>

      <section className="card">
        <div className="card-head"><h3>History</h3><span className="muted small">Stored in <code>backend/data/history.db</code></span></div>
        <div className="form-grid">
          <label className="field"><span>Keep console history (days)</span>
            <input type="number" min={1} value={cfg.history.console_retention_days} onChange={(e) => upd("history", { console_retention_days: num(e.target.value) })} /></label>
          <label className="field"><span>Keep audit log (days)</span>
            <input type="number" min={1} value={cfg.history.audit_retention_days} onChange={(e) => upd("history", { audit_retention_days: num(e.target.value) })} /></label>
          <label className="check field-check">
            <input type="checkbox" checked={cfg.history.persist_console} onChange={(e) => upd("history", { persist_console: e.target.checked })} />
            Save console output to history
          </label>
        </div>
      </section>

      <section className="card">
        <div className="card-head"><h3>Cloud logs</h3><span className="muted small">The log server used by the Cloud logs page</span></div>
        <div className="form-grid">
          <label className="field"><span>Source</span>
            <select value={cfg.logs.source} onChange={(e) => upd("logs", { source: e.target.value as "ssh" | "local" })}>
              <option value="ssh">SSH / SFTP server</option>
              <option value="local">Folder on this PC</option>
            </select></label>
          {cfg.logs.source === "ssh" ? (
            <>
              <label className="field"><span>Host</span>
                <input value={cfg.logs.host} onChange={(e) => upd("logs", { host: e.target.value })} /></label>
              <label className="field"><span>Port</span>
                <input type="number" min={1} max={65535} value={cfg.logs.port} onChange={(e) => upd("logs", { port: num(e.target.value) })} /></label>
              <label className="field"><span>User</span>
                <input value={cfg.logs.username} onChange={(e) => upd("logs", { username: e.target.value })} /></label>
              <label className="field wide"><span>Private key file on this PC (e.g. C:\Users\you\.ssh\ssh-key-2024-05-24.key)</span>
                <input value={cfg.logs.key_path} onChange={(e) => upd("logs", { key_path: e.target.value })} /></label>
              <label className="field"><span>Key passphrase (if any)</span>
                <input type="password" autoComplete="new-password" value={cfg.logs.key_passphrase}
                       onChange={(e) => upd("logs", { key_passphrase: e.target.value })} /></label>
              <label className="field"><span>log.py folder (date / shed layout)</span>
                <input value={cfg.logs.root} onChange={(e) => upd("logs", { root: e.target.value })} /></label>
              <label className="field"><span>dump_logs.py folder (date / MAC layout)</span>
                <input value={cfg.logs.mac_root} onChange={(e) => upd("logs", { mac_root: e.target.value })} /></label>
              <label className="field"><span>"By device" looks back (days)</span>
                <input type="number" min={1} value={cfg.logs.mac_scan_days} onChange={(e) => upd("logs", { mac_scan_days: num(e.target.value) })} /></label>
            </>
          ) : (
            <>
              <label className="field wide"><span>Base folder on this PC (contains the two folders below)</span>
                <input value={cfg.logs.local_root} onChange={(e) => upd("logs", { local_root: e.target.value })} /></label>
              <label className="field"><span>Date / shed folder</span>
                <input value={cfg.logs.root} onChange={(e) => upd("logs", { root: e.target.value })} /></label>
              <label className="field"><span>Per-device folder</span>
                <input value={cfg.logs.mac_root} onChange={(e) => upd("logs", { mac_root: e.target.value })} /></label>
            </>
          )}
          <label className="field"><span>Live follow refresh (s)</span>
            <input type="number" step="0.5" min={0.3} value={cfg.logs.follow_interval_s}
                   onChange={(e) => upd("logs", { follow_interval_s: num(e.target.value) })} /></label>
        </div>
        <div className="row wrap">
          <button className="btn small" disabled={dirty} title={dirty ? "Save settings first" : ""}
                  onClick={async () => {
                    setLogTest({ ok: true, text: "Connecting…" });
                    try { const r = await api.logsTest(); setLogTest({ ok: true, text: `Connected in ${r.ms} ms — ${r.devices} device folder(s); ${r.dates} day folder(s), latest ${r.latest ?? "—"}` }); }
                    catch (e) { setLogTest({ ok: false, text: (e as Error).message }); }
                  }}>Test connection</button>
          {dirty && <span className="muted small">Save settings first</span>}
          {logTest && <span className={logTest.ok ? "ok-text" : "inline-error"}>{logTest.text}</span>}
        </div>
        <p className="muted small">The key is read by the server process — when it runs as the Windows service, keep the key in a folder that
          service can read (your user profile is fine). The server's host key is remembered on first connect (<code>data/ssh_known_hosts</code>).</p>
      </section>

      <div className="save-bar">
        <button className="btn primary" disabled={!dirty} onClick={save}>Save settings</button>
        <button className="btn ghost" disabled={!dirty} onClick={() => { setCfg(saved); setMsg(null); }}>Revert</button>
        {msg && <span className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</span>}
      </div>

      <section className="card">
        <div className="card-head"><h3>About this server</h3></div>
        <dl className="about">
          <dt>Signed in as</dt><dd>{me ? `${me.user} (auth: ${me.auth_mode})` : "—"}</dd>
          <dt>Message definitions</dt><dd><code>{catalog?.messages_dir ?? "not found"}</code></dd>
          <dt>Loaded</dt><dd>{catalog ? `${catalog.groups.reduce((n, g) => n + g.messages.length, 0)} messages, ${catalog.config_keys.length} config keys` : "—"}</dd>
          <dt>Version</dt><dd>server API {health?.api_version ?? "?"} · page API {API_VERSION}</dd>
          <dt>Server started</dt><dd>{health ? fmtDateTime(health.started) : "—"}</dd>
        </dl>
        <div className="row wrap">
          <button className="btn small" onClick={onReloadCatalog}>Reload message definitions</button>
          <button className={`btn small ${restart === "confirm" ? "danger" : ""}`} disabled={restart === "waiting"} onClick={doRestart}
                  title="Loads code updated on disk. Works when running as the Windows service.">
            {restart === "confirm" ? "Confirm restart (OTA / jobs in progress stop)" : restart === "waiting" ? "Restarting…" : "Restart server"}</button>
          {restart === "confirm" && <button className="btn small ghost" onClick={() => setRestart("idle")}>Cancel</button>}
          {restart === "done" && <span className="ok-text">Back up — reloading</span>}
          {restart === "failed" && <span className="inline-error">The server did not come back — check the service logs (deploy/windows/logs)</span>}
        </div>
      </section>
    </div>
  );
}
