import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { useAuth } from "../auth";
import { CommandPanel } from "../components/CommandPanel";
import { ConfigKeysPanel } from "../components/ConfigKeysPanel";
import { DeviceFilters, matchesFilter, NO_FILTER, type SiteFilter } from "../components/DeviceFilters";
import { DeviceInfoBar } from "../components/DeviceInfoBar";
import { PrinterPanel } from "../components/PrinterPanel";
import { seedDevice, useLive } from "../store";
import type { Catalog, Device } from "../types";
import { keysForType, topicId } from "../util";

interface Props {
  catalog: Catalog | null;
  devices: Device[];
  deviceId: string;
  onSelect: (id: string) => void;
}

type Tab = "messages" | "config";
const LS_TAB = "ferp.workspace.tab";

/** State kept for this browser session (survives switching pages). */
function useSessionState<T>(key: string, fallback: T): [T, (v: T) => void] {
  const [value, setValue] = useState<T>(() => {
    try { const s = sessionStorage.getItem(key); return s === null ? fallback : (JSON.parse(s) as T); } catch { return fallback; }
  });
  useEffect(() => { try { sessionStorage.setItem(key, JSON.stringify(value)); } catch { /* storage unavailable */ } }, [key, value]);
  return [value, setValue];
}

export default function Workspace({ catalog, devices, deviceId, onSelect: setDeviceId }: Props) {
  const [topicBase, setTopicBase] = useState<string | null>(null);
  const [search, setSearch] = useSessionState("ferp.workspace.search", "");
  const [siteFilter, setSiteFilter] = useSessionState<SiteFilter>("ferp.workspace.site", NO_FILTER);
  const { can } = useAuth();
  const [tabPick, setTab] = useState<Tab>(() => {
    try { return localStorage.getItem(LS_TAB) === "config" ? "config" : "messages"; } catch { return "messages"; }
  });
  // only tabs this account may use: Messages needs "workspace", Config keys needs "config"
  const tab: Tab = tabPick === "config" ? (can("config") ? "config" : "messages") : (can("workspace") ? "messages" : "config");
  const fleet = useLive((s) => s.fleet);
  const device = devices.find((d) => d.id === deviceId) ?? null;
  const changeTab = (t: Tab) => { setTab(t); try { localStorage.setItem(LS_TAB, t); } catch { /* private mode */ } };

  useEffect(() => {
    setTopicBase(null);
    if (!deviceId || !device) return;
    api.deviceState(deviceId).then((s) => {
      seedDevice(deviceId, s.config, s.devinfo, s.jobs, s.ota);
      setTopicBase(s.topic_base);
    }).catch(() => undefined);
  }, [deviceId, device]);

  // filter fields of a device (board falls back to the HW version the device reported)
  const siteOf = (d: Device) => {
    const live = Object.values(fleet).find((r) => r.device_id === d.id);
    return { device_type: d.device_type, shed: d.shed, pump_type: d.pump_type, board_version: d.board_version || live?.info.hw_version };
  };
  const allRows = useMemo(() => devices.map(siteOf),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [devices, fleet]);
  const q = search.trim().toLowerCase();
  const shown = useMemo(() => devices
    .filter((d) => matchesFilter(siteOf(d), siteFilter))
    .filter((d) => !q || [d.label, d.mac, d.uuid, d.shed, d.pump_id_1, d.pump_id_2, d.notes].some((x) => (x ?? "").toLowerCase().includes(q)))
    .sort((a, b) => a.label.localeCompare(b.label)),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [devices, fleet, siteFilter, q]);
  // the selected device stays selectable even when the filters hide it
  const options = device && !shown.some((d) => d.id === device.id) ? [device, ...shown] : shown;

  return (
    <div className="workspace">
      <section className="card device-bar ws-device">
        <div className="row wrap ws-device-row">
          <input className="search small" placeholder="Search label, MAC, shed, pump…" value={search} onChange={(e) => setSearch(e.target.value)} />
        </div>
        <DeviceFilters rows={allRows} value={siteFilter} onChange={setSiteFilter} />
        <div className="row wrap ws-device-row ws-device-pick">
          <label className="field inline">
            <span>Device</span>
            <select value={device ? deviceId : ""} onChange={(e) => setDeviceId(e.target.value)}>
              <option value="">— select a device ({shown.length}) —</option>
              {options.map((d) => <option key={d.id} value={d.id}>{d.label}{d.shed ? ` · ${d.shed}` : ""}</option>)}
            </select>
          </label>
          {device && (
            <div className="device-meta muted">
              <span>id <code>{topicId(device.uuid || device.mac) || "—"}</code></span>
              <span>group <code>{device.group || "default"}</code></span>
              {topicBase && <span className="hide-sm">topic <code>{topicBase}/…</code></span>}
            </div>
          )}
        </div>
      </section>

      {device && catalog ? (
        <>
          <DeviceInfoBar device={device} keys={catalog.devinfo_keys} />
          <div className="seg ws-tabs" role="tablist" aria-label="Workspace">
            {can("workspace") && <button role="tab" aria-selected={tab === "messages"} className={tab === "messages" ? "active" : ""} onClick={() => changeTab("messages")}>Messages</button>}
            {can("config") && <button role="tab" aria-selected={tab === "config"} className={tab === "config" ? "active" : ""} onClick={() => changeTab("config")}>Config keys</button>}
          </div>
          {tab === "messages" && (
            <div className="ws-col">
              {(device.device_type ?? "").toLowerCase() === "printer" && (
                <PrinterPanel device={device} keys={catalog.config_keys} />
              )}
              <CommandPanel device={device} catalog={catalog} />
            </div>
          )}
          {tab === "config" && (
            <ConfigKeysPanel device={device} keys={keysForType(catalog.config_keys, device.device_type)} />
          )}
        </>
      ) : (
        <section className="card empty">
          {devices.length === 0 ? "No devices yet — add one in the Fleet (+ Add device)." : "Select a device to start."}
        </section>
      )}
    </div>
  );
}
