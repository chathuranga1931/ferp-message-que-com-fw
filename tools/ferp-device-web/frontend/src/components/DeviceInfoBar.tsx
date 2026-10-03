import { useState } from "react";
import { api } from "../api";
import { useLive } from "../store";
import type { Device, DevInfoKeyDef } from "../types";
import { EMPTY } from "../util";

export function DeviceInfoBar({ device, keys }: { device: Device; keys: DevInfoKeyDef[] }) {
  const values = useLive((s) => s.devinfo[device.id]) ?? EMPTY;
  const connected = useLive((s) => s.mqtt?.connected ?? false);
  const [err, setErr] = useState<string | null>(null);

  const read = (k?: number) => {
    setErr(null);
    api.readDevInfo(device.id, k === undefined ? undefined : [k]).catch((e) => setErr(e.message));
  };

  return (
    <section className="card">
      <div className="card-head">
        <h3>Device info</h3>
        <button className="btn small" disabled={!connected} onClick={() => read()}>↻ Read all</button>
      </div>
      {err && <div className="inline-error">{err}</div>}
      <div className="devinfo">
        {keys.map((k) => {
          const v = values[String(k.key)];
          // Like the desktop tool: fall back to the registry value until read from the device.
          const fallback = (device as unknown as Record<string, string>)[k.field] ?? "";
          const text = v ? (v.valid ? v.value : "—") : fallback || "—";
          return (
            <div key={k.key} className={`chip ${v ? "live" : ""}`} title={v ? "Read from device" : "From device registry (not read yet)"}>
              <span className="chip-label">{k.label}</span>
              <span className="chip-value">{text}</span>
              <button className="icon-btn" disabled={!connected} onClick={() => read(k.key)} aria-label={`Read ${k.label}`}>↻</button>
            </div>
          );
        })}
      </div>
    </section>
  );
}
