import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { useLive } from "../store";
import type { Catalog, Device, Favorite, FieldDef, MessageDef, RecentMessage } from "../types";
import { fmtDateTime, hex } from "../util";

const INT_TYPES = ["uint8", "uint16", "uint32", "int8", "int16", "int32"];

function initialValue(f: FieldDef): string {
  const t = f.type ?? "string";
  if (t === "enum" && f.options?.length) {
    const match = f.options.find((o) => String(o.value) === String(f.default));
    return String((match ?? f.options[0]).value);
  }
  if (t === "bool") return f.default ? "true" : "false";
  return f.default === undefined || f.default === null ? "" : String(f.default);
}

/** Typed payload value → form string (to refill the form from a favourite / recent send). */
function toFormValue(f: FieldDef, v: unknown): string {
  if (v === undefined || v === null) return initialValue(f);
  if ((f.type ?? "string") === "bool") return v ? "true" : "false";
  return String(v);
}

/** Convert form strings to typed JSON values (same rules as the desktop tool). */
function buildPayload(fields: FieldDef[], values: Record<string, string>): Record<string, unknown> {
  const data: Record<string, unknown> = {};
  for (const f of fields) {
    const raw = (values[f.name] ?? "").trim();
    const t = f.type ?? "string";
    if (t === "enum") {
      const opt = f.options?.find((o) => String(o.value) === raw);
      data[f.name] = opt ? opt.value : raw;
    } else if (t === "bool") {
      data[f.name] = ["1", "true", "yes"].includes(raw.toLowerCase());
    } else if (INT_TYPES.includes(t)) {
      const n = raw === "" ? 0 : Number(raw);       // Number() accepts 0x.. hex
      if (!Number.isInteger(n)) throw new Error(`Field '${f.label ?? f.name}' expects an integer, got "${raw}"`);
      data[f.name] = n;
    } else if (t === "float" || t === "double") {
      const n = raw === "" ? 0 : Number(raw);
      if (Number.isNaN(n)) throw new Error(`Field '${f.label ?? f.name}' expects a number, got "${raw}"`);
      data[f.name] = n;
    } else {
      data[f.name] = raw;
    }
  }
  return data;
}

export function CommandPanel({ device, catalog }: { device: Device; catalog: Catalog }) {
  const [filter, setFilter] = useState("");
  const [selected, setSelected] = useState<MessageDef | null>(null);
  const [values, setValues] = useState<Record<string, string>>({});
  const [status, setStatus] = useState<{ ok: boolean; text: string } | null>(null);
  const [collapsed, setCollapsed] = useState<Record<string, boolean>>({});
  const connected = useLive((s) => s.mqtt?.connected ?? false);
  const [mode, setMode] = useState<"all" | "fav" | "recent">("all");
  const [favorites, setFavorites] = useState<Favorite[]>([]);
  const [recent, setRecent] = useState<RecentMessage[]>([]);
  const [favName, setFavName] = useState<string | null>(null);

  const reloadLists = () => {
    api.favorites().then(setFavorites).catch(() => undefined);
    api.recentMessages().then(setRecent).catch(() => undefined);
  };
  useEffect(reloadLists, []);

  const byName = useMemo(() => {
    const m: Record<string, MessageDef> = {};
    catalog.groups.forEach((g) => g.messages.forEach((x) => (m[x.name] = x)));
    return m;
  }, [catalog]);

  const groups = useMemo(() => {
    const f = filter.trim().toLowerCase();
    return catalog.groups
      .map((g) => ({ ...g, messages: f ? g.messages.filter((m) => m.name.toLowerCase().includes(f)) : g.messages }))
      .filter((g) => g.messages.length);
  }, [catalog, filter]);

  const select = (m: MessageDef, data?: Record<string, unknown>) => {
    setSelected(m);
    setStatus(null);
    setFavName(null);
    setValues(Object.fromEntries(m.fields.map((f) => [f.name, data ? toFormValue(f, data[f.name]) : initialValue(f)])));
  };

  const selectSaved = (msg: string, data: Record<string, unknown>) => {
    const m = byName[msg];
    if (m) select(m, data);
    else setStatus({ ok: false, text: `${msg} is no longer in the message definitions` });
  };

  const saveFavorite = async () => {
    if (!selected || !favName?.trim()) return;
    try {
      await api.addFavorite(favName.trim(), selected.name, buildPayload(selected.fields, values));
      setFavName(null);
      setStatus({ ok: true, text: "Saved to favourites" });
      reloadLists();
    } catch (e) { setStatus({ ok: false, text: (e as Error).message }); }
  };

  const send = async () => {
    if (!selected) return;
    try {
      const data = buildPayload(selected.fields, values);
      const r = await api.send(device.id, selected.name, data);
      setStatus({ ok: true, text: `Sent ${selected.name} (seq ${r.seq}) — responses appear in the console` });
      api.recentMessages().then(setRecent).catch(() => undefined);
    } catch (e) {
      setStatus({ ok: false, text: (e as Error).message });
    }
  };

  const sendable = selected && selected.direction !== "resp";

  return (
    <section className="card command">
      <div className="card-head"><h3>Messages</h3></div>
      <div className="msg-split">
        <div className="msg-tree">
          <div className="seg">
            <button className={mode === "all" ? "active" : ""} onClick={() => setMode("all")}>All</button>
            <button className={mode === "fav" ? "active" : ""} onClick={() => setMode("fav")}>★ Favourites{favorites.length ? ` (${favorites.length})` : ""}</button>
            <button className={mode === "recent" ? "active" : ""} onClick={() => setMode("recent")}>Recent</button>
          </div>
          {mode === "fav" && (
            <div className="tree-scroll">
              {favorites.map((f) => (
                <div key={f.id} className="saved-item">
                  <button className="tree-item" onClick={() => selectSaved(f.msg, f.data)}>
                    <span>★ {f.name}</span><span className="muted small">{f.msg.replace(/^Msg/, "")}</span>
                  </button>
                  <button className="icon-btn" aria-label={`Delete ${f.name}`} title="Remove favourite"
                          onClick={() => api.deleteFavorite(f.id).then(reloadLists)}>×</button>
                </div>
              ))}
              {favorites.length === 0 && <p className="muted pad">No favourites yet — build a command and press ☆ Save.</p>}
            </div>
          )}
          {mode === "recent" && (
            <div className="tree-scroll">
              {recent.map((r, i) => (
                <button key={i} className="tree-item" onClick={() => selectSaved(r.msg, r.data)}
                        title={`${JSON.stringify(r.data)}
${fmtDateTime(r.ts)}${r.device_label ? ` → ${r.device_label}` : ""}`}>
                  <span>{r.msg.replace(/^Msg/, "")}</span>
                  <span className="muted small ellipsis">{Object.keys(r.data).length ? JSON.stringify(r.data) : "{}"}</span>
                </button>
              ))}
              {recent.length === 0 && <p className="muted pad">Nothing sent yet.</p>}
            </div>
          )}
          {mode === "all" && <input className="search" placeholder="Filter messages…" value={filter} onChange={(e) => setFilter(e.target.value)} />}
          <div className="tree-scroll" hidden={mode !== "all"}>
            {groups.map((g) => (
              <div key={g.group} className="tree-group">
                <button className="tree-group-head" onClick={() => setCollapsed({ ...collapsed, [g.group]: !collapsed[g.group] })}>
                  <span>{collapsed[g.group] && !filter ? "▸" : "▾"} {g.group}</span><span className="muted">{g.messages.length}</span>
                </button>
                {(!collapsed[g.group] || filter) && g.messages.map((m) => (
                  <button key={m.name} className={`tree-item ${selected?.name === m.name ? "active" : ""}`} onClick={() => select(m)}>
                    <span>{m.name.replace(/^Msg/, "")}</span>
                    <span className={`dir ${m.direction}`}>{m.direction}</span>
                  </button>
                ))}
              </div>
            ))}
          </div>
        </div>

        <div className="msg-form">
          {!selected ? (
            <p className="muted">Select a message to build a command.</p>
          ) : (
            <>
              <div className="msg-title">
                <b>{selected.name}</b>
                <span className="muted"> {hex(selected.msg_id)} · {selected.fields.length} field(s)</span>
                <span className={`dir ${selected.direction}`}>{selected.direction}</span>
              </div>
              {selected.fields.length === 0 && <p className="muted">No payload fields — sends an empty {"{}"}.</p>}
              <div className="form-grid">
                {selected.fields.map((f) => (
                  <label key={f.name} className="field">
                    <span>{f.label ?? f.name}</span>
                    <FieldInput f={f} value={values[f.name] ?? ""} onChange={(v) => setValues({ ...values, [f.name]: v })} />
                  </label>
                ))}
              </div>
              {favName !== null && (
                <div className="row">
                  <input autoFocus placeholder="Favourite name" value={favName} onChange={(e) => setFavName(e.target.value)}
                         onKeyDown={(e) => e.key === "Enter" && saveFavorite()} />
                  <button className="btn small primary" disabled={!favName.trim()} onClick={saveFavorite}>Save</button>
                  <button className="btn small ghost" onClick={() => setFavName(null)}>Cancel</button>
                </div>
              )}
              <div className="row">
                {sendable && favName === null && <button className="btn ghost" onClick={() => setFavName(selected.name.replace(/^Msg/, ""))}>☆ Save</button>}
                <button className="btn primary" disabled={!sendable || !connected} onClick={send}
                        title={!connected ? "MQTT is not connected" : !sendable ? "Response-only message" : undefined}>Send</button>
                {status && <span className={status.ok ? "ok-text" : "inline-error"}>{status.text}</span>}
              </div>
            </>
          )}
        </div>
      </div>
    </section>
  );
}

function FieldInput({ f, value, onChange }: { f: FieldDef; value: string; onChange: (v: string) => void }) {
  const t = f.type ?? "string";
  if (t === "enum" && f.options?.length) {
    return (
      <select value={value} onChange={(e) => onChange(e.target.value)}>
        {f.options.map((o) => <option key={String(o.value)} value={String(o.value)}>{String(o.label)}</option>)}
      </select>
    );
  }
  if (t === "bool") {
    return (
      <select value={value} onChange={(e) => onChange(e.target.value)}>
        <option value="true">true</option><option value="false">false</option>
      </select>
    );
  }
  return <input value={value} placeholder={f.placeholder ?? t} onChange={(e) => onChange(e.target.value)}
                inputMode={INT_TYPES.includes(t) ? "numeric" : undefined} />;
}
