import { useCallback, useEffect, useState } from "react";
import { api } from "./api";
import { ConsoleDock } from "./components/ConsoleDock";
import { MqttControl } from "./components/MqttControl";
import Cables from "./pages/Cables";
import Definitions from "./pages/Definitions";
import DevicePage from "./pages/DevicePage";
import Devices from "./pages/Devices";
import Fleet from "./pages/Fleet";
import History from "./pages/History";
import Logs from "./pages/Logs";
import Ota from "./pages/Ota";
import Settings from "./pages/Settings";
import Sheds from "./pages/Sheds";
import Snapshots from "./pages/Snapshots";
import Workspace from "./pages/Workspace";
import { useLive } from "./store";
import type { Catalog, Device } from "./types";
import { API_VERSION } from "./version";

type Page = "fleet" | "device" | "workspace" | "ota" | "logs" | "snapshots" | "history" | "devices" | "sheds" | "cables"
  | "definitions" | "settings";
const PAGES: { id: Page; label: string }[] = [
  { id: "fleet", label: "Fleet" },
  { id: "device", label: "Device" },
  { id: "workspace", label: "Workspace" },
  { id: "ota", label: "OTA" },
  { id: "logs", label: "Cloud logs" },
  { id: "snapshots", label: "Snapshots" },
  { id: "history", label: "History" },
  { id: "devices", label: "Devices" },
  { id: "sheds", label: "Sheds" },
  { id: "cables", label: "Cables" },
  { id: "definitions", label: "Definitions" },
  { id: "settings", label: "Settings" },
];
const LS_KEY = "ferp.selectedDevice";

function initialPage(): Page {
  const h = location.hash.replace("#", "");
  return PAGES.find((p) => p.id === h)?.id ?? "fleet";
}

/** ?device=<id> (bookmarkable) wins over the last selection kept in this browser. */
function initialDevice(): string {
  const fromUrl = new URLSearchParams(location.search).get("device");
  if (fromUrl) return fromUrl;
  try { return localStorage.getItem(LS_KEY) ?? ""; } catch { return ""; }
}

export default function App() {
  const [page, setPage] = useState<Page>(initialPage);
  const [deviceId, setDeviceId] = useState<string>(initialDevice);
  const [otaPreselect, setOtaPreselect] = useState<string[]>([]);
  const [shedFocus, setShedFocus] = useState("");
  const [catalog, setCatalog] = useState<Catalog | null>(null);
  const [devices, setDevices] = useState<Device[]>([]);
  const [error, setError] = useState<string | null>(null);
  const wsConnected = useLive((s) => s.wsConnected);
  const serverApi = useLive((s) => s.serverApiVersion);

  const reloadDevices = useCallback(() => api.devices().then(setDevices).catch((e) => setError(String(e.message))), []);
  const reloadCatalog = useCallback((fresh = false) =>
    (fresh ? api.reloadCatalog() : api.catalog()).then(setCatalog).catch((e) => setError(String(e.message))), []);

  useEffect(() => { reloadDevices(); reloadCatalog(); }, [reloadDevices, reloadCatalog]);
  useEffect(() => { location.hash = page; }, [page]);
  useEffect(() => { try { localStorage.setItem(LS_KEY, deviceId); } catch { /* storage unavailable */ } }, [deviceId]);

  /** A device's own page (details, lifecycle, history); the Workspace is one click from there. */
  const openDevice = useCallback((id: string) => { setDeviceId(id); setPage("device"); }, []);
  const openWorkspace = useCallback((id: string) => { setDeviceId(id); setPage("workspace"); }, []);
  const openSheds = useCallback((name = "") => { setShedFocus(name); setPage("sheds"); }, []);
  const openOta = useCallback((ids: string[]) => { setOtaPreselect(ids); setPage("ota"); }, []);
  const clearPreselect = useCallback(() => setOtaPreselect([]), []);
  /** Cloud logs → "By device" view for this MAC (dump_logs.py folders). */
  const openLogs = useCallback((mac: string) => {
    try {
      localStorage.setItem("ferp.logs.mode", "device");
      localStorage.setItem("ferp.logs.mac", mac);
    } catch { /* storage unavailable */ }
    setPage("logs");
  }, []);
  const device = devices.find((d) => d.id === deviceId) ?? null;
  const versionMismatch = serverApi !== null && serverApi !== API_VERSION;

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">FERP <span>Device Web</span></div>
        <nav className="tabs">
          {PAGES.map((p) => (
            <button key={p.id} className={page === p.id ? "tab active" : "tab"} onClick={() => setPage(p.id)}>{p.label}</button>
          ))}
        </nav>
        <MqttControl />
      </header>
      {versionMismatch && (
        <div className="banner error">
          {serverApi! < API_VERSION
            ? <>The server is running an older version than this page (API {serverApi} vs {API_VERSION}). Restart it: <code>Restart-Service ferp-device-web</code> or Settings → Restart server.</>
            : <>This page is out of date (API {API_VERSION} vs server {serverApi}). <button className="link-btn" onClick={() => location.reload()}>Reload</button></>}
        </div>
      )}
      {!wsConnected && <div className="banner warn">Live connection to the server lost — reconnecting…</div>}
      {error && <div className="banner error" onClick={() => setError(null)}>{error} (click to dismiss)</div>}
      <main className="page-scroll">
        <div className="page">
          {page === "fleet" && <Fleet devices={devices} onOpen={openDevice} onOta={openOta} onLogs={openLogs} onDevicesChanged={reloadDevices} />}
          {page === "device" && <DevicePage deviceId={deviceId} devices={devices} onSelect={setDeviceId} onWorkspace={openWorkspace}
                                             onOta={openOta} onLogs={openLogs} onSheds={openSheds} onChanged={reloadDevices} />}
          {page === "workspace" && <Workspace catalog={catalog} devices={devices} deviceId={deviceId} onSelect={setDeviceId} onOta={openOta} />}
          {page === "ota" && <Ota devices={devices} preselect={otaPreselect} onPreselectUsed={clearPreselect} />}
          {page === "logs" && <Logs devices={devices} onOpenSettings={() => setPage("settings")} />}
          {page === "snapshots" && <Snapshots catalog={catalog} devices={devices} />}
          {page === "history" && <History devices={devices} />}
          {page === "devices" && <Devices devices={devices} onChanged={reloadDevices} onLogs={openLogs} onOpen={openDevice} />}
          {page === "sheds" && <Sheds devices={devices} focusName={shedFocus} onChanged={reloadDevices} onOpenDevice={openDevice} />}
          {page === "cables" && <Cables devices={devices} onOpenDevice={openDevice} />}
          {page === "definitions" && <Definitions />}
          {page === "settings" && <Settings catalog={catalog} onReloadCatalog={() => reloadCatalog(true)} />}
        </div>
      </main>
      <ConsoleDock device={device} devices={devices} />
    </div>
  );
}
