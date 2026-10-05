import { useEffect, useState } from "react";
import { api } from "../api";
import { CommandPanel } from "../components/CommandPanel";
import { ConfigKeysPanel } from "../components/ConfigKeysPanel";
import { DeviceInfoBar } from "../components/DeviceInfoBar";
import { PrinterPanel } from "../components/PrinterPanel";
import { seedDevice } from "../store";
import type { Catalog, Device } from "../types";
import { keysForType, topicId } from "../util";

interface Props {
  catalog: Catalog | null;
  devices: Device[];
  deviceId: string;
  onSelect: (id: string) => void;
  onOta: (ids: string[]) => void;
}

export default function Workspace({ catalog, devices, deviceId, onSelect: setDeviceId, onOta }: Props) {
  const [topicBase, setTopicBase] = useState<string | null>(null);
  const device = devices.find((d) => d.id === deviceId) ?? null;

  useEffect(() => {
    setTopicBase(null);
    if (!deviceId || !device) return;
    api.deviceState(deviceId).then((s) => {
      seedDevice(deviceId, s.config, s.devinfo, s.jobs, s.ota);
      setTopicBase(s.topic_base);
    }).catch(() => undefined);
  }, [deviceId, device]);

  return (
    <div className="workspace">
      <section className="card device-bar">
        <label className="field inline">
          <span>Device</span>
          <select value={device ? deviceId : ""} onChange={(e) => setDeviceId(e.target.value)}>
            <option value="">— select a device —</option>
            {devices.map((d) => <option key={d.id} value={d.id}>{d.label}</option>)}
          </select>
        </label>
        {device && (
          <div className="device-meta muted">
            <span>id <code>{topicId(device.uuid || device.mac) || "—"}</code></span>
            <span>group <code>{device.group || "default"}</code></span>
            {topicBase && <span className="hide-sm">topic <code>{topicBase}/…</code></span>}
          </div>
        )}
        {device && <button className="btn small device-ota" onClick={() => onOta([device.id])}>OTA…</button>}
      </section>

      {device && catalog ? (
        <>
          <DeviceInfoBar device={device} keys={catalog.devinfo_keys} />
          <div className="ws-grid">
            <div className="ws-col">
              {(device.device_type ?? "").toLowerCase() === "printer" && (
                <PrinterPanel device={device} keys={catalog.config_keys} />
              )}
              <CommandPanel device={device} catalog={catalog} />
            </div>
            <div className="ws-col">
              <ConfigKeysPanel device={device} keys={keysForType(catalog.config_keys, device.device_type)} />
            </div>
          </div>
        </>
      ) : (
        <section className="card empty">
          {devices.length === 0 ? "No devices yet — add one on the Devices page." : "Select a device to start."}
        </section>
      )}
    </div>
  );
}
