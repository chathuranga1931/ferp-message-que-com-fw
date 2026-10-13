import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { useAuth } from "../auth";
import { DeviceRecords } from "./DeviceRecords";
import { useLive } from "../store";
import type { Device, DeviceIn, FleetRow } from "../types";
import { fmtAgo, fmtDateTime, topicId, useNow } from "../util";

const BLANK: DeviceIn = {
  label: "", mac: "", uuid: "", group: "default", ip: "", notes: "",
  shed: "", pump_id_1: "", pump_id_2: "", sd_card_size: "", board_version: "", pump_type: "", device_type: "COM",
};
const DEVICE_TYPES = ["COM", "Printer"];
const FIELDS = Object.keys(BLANK) as (keyof DeviceIn)[];
const SD_SIZES = ["4 GB", "8 GB", "16 GB", "32 GB", "64 GB"];
const PUMP_ID_MAX = 4;     // firmware NOZZLE_x_ID limit

/** Distinct non-empty values of a field across devices (for suggestions). */
function distinct(devices: Device[], k: keyof DeviceIn, extra: string[] = []): string[] {
  return [...new Set([...extra, ...devices.map((d) => d[k]).filter(Boolean)])].sort();
}

interface Props {
  /** device to show, or "new" to add one */
  device: Device | "new";
  devices: Device[];
  onClose: () => void;
  onChanged: () => void;
  onLogs: (mac: string) => void;
  onWorkspace: (id: string) => void;
  onOta: (ids: string[]) => void;
  onSheds: (name?: string) => void;
  /** "new" only: values to start the add form with (e.g. the shed it is added from) */
  initial?: Partial<DeviceIn>;
}

type Tab = "details" | "lifecycle" | "history";

/** Pop-up with everything about one device: details (view / edit / delete), lifecycle, history.
 *  "new" opens the add form. */
export function DeviceDialog({ device, devices, onClose, onChanged, onLogs, onWorkspace, onOta, onSheds, initial }: Props) {
  const { can } = useAuth();
  const isNew = device === "new";
  const dev = isNew ? null : device;
  const [tab, setTab] = useState<Tab>("details");
  const [openIssues, setOpenIssues] = useState(0);
  // open-issue count for the History tab label (the History tab itself keeps it current while open)
  useEffect(() => {
    if (!dev) return;
    api.deviceLog(dev.id).then((l) => setOpenIssues(l.filter((e) => e.kind === "issue" && e.status === "open").length)).catch(() => undefined);
  }, [dev?.id]);
  const [editing, setEditing] = useState(isNew);
  const [form, setForm] = useState<DeviceIn>(BLANK);
  const [confirmDelete, setConfirmDelete] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const [info, setInfo] = useState<string | null>(null);
  const [reading, setReading] = useState(false);
  const connected = useLive((s) => s.mqtt?.connected ?? false);
  const fleet = useLive((s) => s.fleet);
  const timeout = useLive((s) => s.fleetTimeout);
  const skew = useLive((s) => s.clockSkew);
  const now = useNow(5000) + skew;
  const live: FleetRow | undefined = dev ? Object.values(fleet).find((r) => r.device_id === dev.id) : undefined;

  const startEdit = () => {
    setErr(null); setInfo(null);
    setForm(dev ? (Object.fromEntries(FIELDS.map((k) => [k, dev[k] ?? ""])) as unknown as DeviceIn) : { ...BLANK, ...initial });
    setEditing(true);
  };
  useEffect(() => { if (isNew) startEdit(); /* eslint-disable-next-line react-hooks/exhaustive-deps */ }, [isNew]);
  // Esc closes
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => { if (e.key === "Escape") onClose(); };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  const save = async () => {
    if (!form.label.trim()) { setErr("Label is required"); return; }
    if (!form.mac.trim() && !form.uuid.trim()) { setErr("Enter a MAC or a UUID — MQTT topics need one of them"); return; }
    try {
      if (isNew) { await api.addDevice(form); onChanged(); onClose(); return; }
      await api.updateDevice(dev!.id, { ...dev!, ...form });
      setEditing(false);
      onChanged();
    } catch (e) { setErr((e as Error).message); }
  };

  const readFromDevice = async () => {
    if (!dev) return;
    setReading(true); setErr(null); setInfo(null);
    try {
      const r = await api.readSiteInfo(dev.id);
      const got = Object.entries(r.values);
      setForm((f) => ({ ...f, ...r.values }));
      setInfo(got.length ? `Read from device: ${got.map(([k, v]) => `${k.replace(/_/g, " ")} = ${v || "(empty)"}`).join(", ")} — review and Save` : "");
      if (r.errors.length) setErr(r.errors.join("; "));
    } catch (e) { setErr((e as Error).message); }
    finally { setReading(false); }
  };

  const remove = async () => {
    if (!dev) return;
    if (!confirmDelete) { setConfirmDelete(true); return; }
    try { await api.deleteDevice(dev.id); onChanged(); onClose(); }
    catch (e) { setErr((e as Error).message); setConfirmDelete(false); }
  };

  const sheds = useMemo(() => distinct(devices, "shed"), [devices]);
  const set = (k: keyof DeviceIn) => (e: React.ChangeEvent<HTMLInputElement>) => setForm({ ...form, [k]: e.target.value });
  const longId = [form.pump_id_1, form.pump_id_2].some((x) => x.length > PUMP_ID_MAX);
  const mac = dev ? topicId(dev.mac) : "";
  const status = !live?.last_seen ? "Never seen" : now - live.last_seen <= timeout ? "Online" : "Offline";
  const dash = (v?: string | null) => v || <span className="muted">—</span>;

  return (
    <div className="modal-backdrop" onMouseDown={(e) => { if (e.target === e.currentTarget) onClose(); }}>
      <div className="modal" role="dialog" aria-modal="true" aria-label={isNew ? "Add device" : dev!.label}>
        <div className="modal-head">
          <h3>
            {isNew ? "Add device" : dev!.label}
            {dev?.device_type && <span className={`type-badge t-${dev.device_type.toLowerCase()}`}>{dev.device_type}</span>}
          </h3>
          <span className="spacer" />
          {dev && (
            <div className="row wrap">
              {(can("workspace") || can("config")) && <button className="btn small" onClick={() => { onWorkspace(dev.id); onClose(); }} title="Send messages, read / write config">Workspace</button>}
              {can("ota") && <button className="btn small" onClick={() => { onOta([dev.id]); onClose(); }}>OTA…</button>}
              {mac && can("logs") && <button className="btn small ghost" onClick={() => { onLogs(mac); onClose(); }} title="Cloud logs of this device (by MAC)">Logs</button>}
            </div>
          )}
          <button className="modal-close" onClick={onClose} aria-label="Close" title="Close (Esc)">×</button>
        </div>

        {dev && (
          <div className="seg modal-tabs">
            <button className={tab === "details" ? "active" : ""} onClick={() => setTab("details")}>Details</button>
            <button className={tab === "lifecycle" ? "active" : ""} onClick={() => setTab("lifecycle")}>Lifecycle</button>
            <button className={tab === "history" ? "active" : ""} onClick={() => setTab("history")}>
              History{openIssues > 0 && <span className="pill err">{openIssues} open</span>}</button>
          </div>
        )}
        {dev && tab !== "details" && (
          <DeviceRecords device={dev} section={tab} onSheds={(n) => { onSheds(n); onClose(); }} onChanged={onChanged} onOpenIssues={setOpenIssues} />
        )}

        {tab === "details" && !editing && dev && (
          <>
            <div className="form-section">Live</div>
            <dl className="kv">
              <dt>Status</dt><dd><span className={`dot ${status === "Online" ? "connected" : status === "Offline" ? "disconnected" : ""}`} /> {status}
                {live?.last_seen ? <span className="muted small"> · {fmtAgo(live.last_seen, now)} ({fmtDateTime(live.last_seen)})</span> : null}</dd>
              <dt>FW version</dt><dd><code>{live?.info.fw_version ?? "—"}</code></dd>
              <dt>HW version</dt><dd><code>{live?.info.hw_version ?? "—"}</code></dd>
              <dt>Last message</dt><dd>{live?.last_msg ? `${live.last_kind} ${live.last_msg.replace(/^Msg/, "")}` : "—"}</dd>
            </dl>
            <div className="form-section">Identity</div>
            <dl className="kv">
              <dt>Device type</dt><dd>{dash(dev.device_type)}</dd>
              <dt>MQTT group</dt><dd>{dash(dev.group)}</dd>
              <dt>MAC</dt><dd>{dev.mac ? <code>{dev.mac}</code> : dash("")}</dd>
              <dt>UUID</dt><dd>{dev.uuid ? <code>{dev.uuid}</code> : dash("")}</dd>
              <dt>Topic id</dt><dd><code>{topicId(dev.uuid || dev.mac)}</code></dd>
            </dl>
            <div className="form-section">Site &amp; hardware</div>
            <dl className="kv">
              <dt>Shed</dt><dd>{dash(dev.shed)}</dd>
              <dt>Pump IDs</dt><dd>{dash([dev.pump_id_1, dev.pump_id_2].filter(Boolean).join(" / "))}</dd>
              <dt>Pump type</dt><dd>{dash(dev.pump_type)}</dd>
              <dt>Board version</dt><dd>{dash(dev.board_version)}</dd>
              <dt>SD card</dt><dd>{dash(dev.sd_card_size)}</dd>
              <dt>Notes</dt><dd>{dash(dev.notes)}</dd>
            </dl>
            {err && <div className="inline-error">{err}</div>}
            <div className="row wrap modal-actions">
              {can("devices") && <button className="btn primary" onClick={startEdit}>Edit</button>}
              <span className="spacer" />
              {can("devices") && <button className={`btn ${confirmDelete ? "danger" : "ghost"}`} onClick={remove} onBlur={() => setConfirmDelete(false)}
                      title="Remove from this app's device list (the device itself, its config and its logs are not touched)">
                {confirmDelete ? "Confirm — remove from the list" : "Remove from list"}</button>}
            </div>
          </>
        )}

        {tab === "details" && editing && (
          <>
            <div className="form-section">Identity</div>
            <div className="form-grid">
              <label className="field"><span>Label *</span><input value={form.label} onChange={set("label")} autoFocus /></label>
              <label className="field"><span>Device type</span>
                <input list="dl-types" value={form.device_type} onChange={set("device_type")} placeholder="COM / Printer" /></label>
              <label className="field"><span>MQTT group</span><input value={form.group} onChange={set("group")} /></label>
              <label className="field"><span>MAC</span><input value={form.mac} onChange={set("mac")} placeholder="f8b3b73988c8" /></label>
              <label className="field"><span>UUID (if provisioned)</span><input value={form.uuid} onChange={set("uuid")} /></label>
            </div>

            <div className="form-section">
              Site &amp; hardware
              {!isNew && (
                <button className="btn small" disabled={!connected || reading} onClick={readFromDevice}
                        title="Reads pump IDs (NOZZLE_0/1_ID) and board version (HW_VERSION) from the device">
                  {reading ? "Reading…" : "Read from device"}</button>
              )}
            </div>
            <div className="form-grid">
              <label className="field"><span>Shed name</span>
                <input list="dl-sheds" value={form.shed} onChange={set("shed")} placeholder="e.g. YAKKALA" /></label>
              <label className="field"><span>Pump ID 1 (nozzle 1)</span>
                <input value={form.pump_id_1} onChange={set("pump_id_1")} placeholder="e.g. P01" /></label>
              <label className="field"><span>Pump ID 2 (nozzle 2)</span>
                <input value={form.pump_id_2} onChange={set("pump_id_2")} placeholder="e.g. P02" /></label>
              <label className="field"><span>Pump type</span>
                <input list="dl-pumptypes" value={form.pump_type} onChange={set("pump_type")} /></label>
              <label className="field"><span>Board version</span>
                <input list="dl-boards" value={form.board_version} onChange={set("board_version")} placeholder="e.g. 2404" /></label>
              <label className="field"><span>SD card size</span>
                <input list="dl-sd" value={form.sd_card_size} onChange={set("sd_card_size")} placeholder="e.g. 8 GB" /></label>
              <label className="field wide"><span>Notes</span><input value={form.notes} onChange={set("notes")} /></label>
            </div>
            <datalist id="dl-sheds">{sheds.map((s) => <option key={s} value={s} />)}</datalist>
            <datalist id="dl-types">{distinct(devices, "device_type", DEVICE_TYPES).map((s) => <option key={s} value={s} />)}</datalist>
            <datalist id="dl-pumptypes">{distinct(devices, "pump_type").map((s) => <option key={s} value={s} />)}</datalist>
            <datalist id="dl-boards">{distinct(devices, "board_version", ["2308", "2404", "2602"]).map((s) => <option key={s} value={s} />)}</datalist>
            <datalist id="dl-sd">{distinct(devices, "sd_card_size", SD_SIZES).map((s) => <option key={s} value={s} />)}</datalist>

            {longId && <div className="inline-error small">The device stores at most {PUMP_ID_MAX} characters per pump ID.</div>}
            <p className="muted small">
              Topics: <code>ferp/&lt;dev_type&gt;/{form.group || "default"}/{topicId(form.uuid || form.mac) || "…"}/cmd</code>
              {" "}— UUID is used when set, otherwise the MAC. Pump IDs on the device itself are changed in the Workspace (config keys NOZZLE_0_ID / NOZZLE_1_ID).
            </p>
            {info && <div className="ok-text">{info}</div>}
            {err && <div className="inline-error">{err}</div>}
            <div className="row modal-actions">
              <button className="btn primary" onClick={save}>{isNew ? "Add device" : "Save"}</button>
              <button className="btn ghost" onClick={() => (isNew ? onClose() : setEditing(false))}>Cancel</button>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
