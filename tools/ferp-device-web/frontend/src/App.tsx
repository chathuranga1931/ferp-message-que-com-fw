import { useCallback, useEffect, useState } from "react";
import { api } from "./api";
import { useAuth, type Permission } from "./auth";
import { ConsoleDock } from "./components/ConsoleDock";
import { DeviceDialog } from "./components/DeviceDialog";
import { MqttControl } from "./components/MqttControl";
import Cables from "./pages/Cables";
import Definitions from "./pages/Definitions";
import Fleet from "./pages/Fleet";
import Guide from "./pages/Guide";
import History from "./pages/History";
import Logs from "./pages/Logs";
import Releases from "./pages/Releases";
import Ota from "./pages/Ota";
import Settings from "./pages/Settings";
import Sheds from "./pages/Sheds";
import Snapshots from "./pages/Snapshots";
import Users from "./pages/Users";
import Workspace from "./pages/Workspace";
import { useLive } from "./store";
import type { Catalog, Device } from "./types";
import { API_VERSION } from "./version";

type Page = "fleet" | "workspace" | "ota" | "logs" | "snapshots" | "history" | "sheds" | "cables"
  | "def-pumps" | "def-cables" | "users" | "guide-troubleshooting" | "guide-installation" | "guide-ota" | "guide-devices"
  | "info-pumps" | "releases" | "settings";
/** `need`: shown when the user has any of these permissions; "admin" = admins only; none = everyone */
interface PageDef { id: Page; label: string; icon: string; need?: Permission[] | "admin" }
const PAGES: PageDef[] = [
  { id: "fleet", label: "Fleet", icon: "▦" },
  { id: "workspace", label: "Workspace", icon: "⌨", need: ["workspace", "config"] },
  { id: "ota", label: "OTA", icon: "⇪" },
  { id: "logs", label: "Cloud logs", icon: "☰", need: ["logs"] },
  { id: "snapshots", label: "Snapshots", icon: "⧉", need: ["config"] },
  { id: "history", label: "History", icon: "↺", need: ["history"] },
  { id: "sheds", label: "Sheds", icon: "⛽" },
  { id: "cables", label: "Cables", icon: "⌁" },
  { id: "def-pumps", label: "Pump types", icon: "#" },
  { id: "def-cables", label: "Cable types", icon: "≋" },
  { id: "users", label: "Users", icon: "👤", need: "admin" },
  { id: "guide-troubleshooting", label: "Troubleshooting", icon: "⚠" },
  { id: "guide-installation", label: "Installation", icon: "⚒" },
  { id: "guide-ota", label: "Firmware OTA", icon: "⤓" },
  { id: "guide-devices", label: "Device types", icon: "▣" },
  { id: "info-pumps", label: "Pump types", icon: "ⓘ" },
  { id: "releases", label: "Releases", icon: "⬢" },
  { id: "settings", label: "Settings", icon: "⚙", need: ["settings"] },
];
/** Sidebar: a group (title + pages) or a single page. */
type NavEntry = { group: string; icon: string; pages: Page[] } | { page: Page };
const NAV: NavEntry[] = [
  { group: "Fleet", icon: "▦", pages: ["fleet", "workspace", "ota", "logs", "snapshots", "history"] },
  { page: "sheds" },
  { page: "cables" },
  { group: "Definitions", icon: "#", pages: ["def-pumps", "def-cables"] },
  { page: "users" },
  { group: "Guides", icon: "?", pages: ["guide-troubleshooting", "guide-installation", "guide-ota", "guide-devices"] },
  { group: "Info", icon: "ⓘ", pages: ["info-pumps"] },
  { page: "releases" },
  { page: "settings" },
];
const LS_KEY = "ferp.selectedDevice";

function initialPage(): Page {
  const h = location.hash.replace("#", "");
  if (h === "definitions") return "def-pumps";        // old link
  return PAGES.find((p) => p.id === h)?.id ?? "fleet";
}

/** ?device=<id> (bookmarkable) wins over the last selection kept in this browser. */
function initialDevice(): string {
  const fromUrl = new URLSearchParams(location.search).get("device");
  if (fromUrl) return fromUrl;
  try { return localStorage.getItem(LS_KEY) ?? ""; } catch { return ""; }
}

export default function App() {
  const { me, admin, can } = useAuth();
  const allowed = (p: (typeof PAGES)[number]) => !p.need || (p.need === "admin" ? admin : p.need.some(can));
  const pages = PAGES.filter(allowed);
  const [page, setPageRaw] = useState<Page>(() => {
    const p = initialPage();
    return pages.some((x) => x.id === p) ? p : "fleet";
  });
  const setPage = useCallback((p: Page) => setPageRaw(PAGES.some((x) => x.id === p && allowed(x)) ? p : "fleet"),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [admin, me]);
  const [deviceId, setDeviceId] = useState<string>(initialDevice);
  const [navOpen, setNavOpen] = useState(false);                     // phone width: sidebar shown
  const go = (p: Page) => { setPage(p); setNavOpen(false); (document.activeElement as HTMLElement | null)?.blur(); };
  // device pop-up (details, lifecycle, history) — ?device=<id> opens it
  const [dialogId, setDialogId] = useState<string | null>(() => new URLSearchParams(location.search).get("device"));
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

  /** A device's pop-up (details, lifecycle, history); the Workspace is one click from there. */
  const openDevice = useCallback((id: string) => { setDeviceId(id); setDialogId(id); }, []);
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
  // "device busy" pop-up (api.ts raises it for HTTP 423); auto-hides after 6 s
  const [busyMsg, setBusyMsg] = useState<string | null>(null);
  useEffect(() => {
    let t: number | undefined;
    const on = (e: Event) => {
      setBusyMsg((e as CustomEvent<string>).detail);
      window.clearTimeout(t);
      t = window.setTimeout(() => setBusyMsg(null), 6000);
    };
    window.addEventListener("ferp-busy", on);
    return () => { window.removeEventListener("ferp-busy", on); window.clearTimeout(t); };
  }, []);
  const device = devices.find((d) => d.id === deviceId) ?? null;
  const versionMismatch = serverApi !== null && serverApi !== API_VERSION;

  return (
    <div className={`app with-sidebar${navOpen ? " nav-open" : ""}`}>
      <aside className="sidebar" aria-label="Main menu">
        <div className="sidebar-brand">FERP <span>Device Web</span></div>
        <nav className="side-nav">
          {NAV.map((e) => {
            if ("page" in e) {
              const p = pages.find((x) => x.id === e.page);
              return p ? (
                <button key={p.id} className={`side-box${page === p.id ? " active" : ""}`} onClick={() => go(p.id)}>
                  <span className="side-ico">{p.icon}</span><span className="side-name">{p.label}</span>
                </button>) : null;
            }
            // a group: only its name is shown; its pages fly out to the right on hover / keyboard focus
            const items = e.pages.map((id) => pages.find((x) => x.id === id)).filter(Boolean) as PageDef[];
            if (items.length === 0) return null;
            const cur = items.find((x) => x.id === page);
            return (
              <div key={e.group} className={`side-group${cur ? " active" : ""}`}>
                <button className="side-box" onClick={() => go((cur ?? items[0]).id)} aria-haspopup="true">
                  <span className="side-ico">{e.icon}</span>
                  <span className="side-name">{e.group}{cur && cur.label !== e.group && <small>{cur.label}</small>}</span>
                  <span className="side-more">▸</span>
                </button>
                <div className="side-flyout" role="menu" aria-label={e.group}>
                  <div className="side-flyout-head">{e.group}</div>
                  {items.map((p) => (
                    <button key={p.id} role="menuitem" className={`side-sub${page === p.id ? " active" : ""}`} onClick={() => go(p.id)}>
                      <span className="side-ico">{p.icon}</span>{p.label}
                    </button>
                  ))}
                </div>
              </div>
            );
          })}
        </nav>
        <div className="sidebar-foot">
          <MqttControl />
          {me?.login_required && (
            <div className="user-menu">
              <span title={admin ? "Admin" : `Permissions: ${me.permissions.join(", ") || "view only"}`}><b>{me.user}</b>{admin ? " · admin" : ""}</span>
              <button className="btn small ghost" onClick={() => api.logout().finally(() => location.reload())}>Sign out</button>
            </div>
          )}
        </div>
      </aside>
      <div className="nav-scrim" onClick={() => setNavOpen(false)} />
      <div className="main-col">
      <header className="mobile-bar">
        <button className="btn small ghost" onClick={() => setNavOpen(true)} aria-label="Open menu">☰</button>
        <span className="sidebar-brand">FERP <span>Device Web</span></span>
        <span className="muted small">{PAGES.find((x) => x.id === page)?.label}</span>
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
          {page === "workspace" && <Workspace catalog={catalog} devices={devices} deviceId={deviceId} onSelect={setDeviceId} />}
          {page === "ota" && <Ota devices={devices} preselect={otaPreselect} onPreselectUsed={clearPreselect} />}
          {page === "logs" && <Logs devices={devices} onOpenSettings={() => setPage("settings")} />}
          {page === "snapshots" && <Snapshots catalog={catalog} devices={devices} />}
          {page === "history" && <History devices={devices} />}
          {page === "sheds" && <Sheds devices={devices} focusName={shedFocus} onChanged={reloadDevices} onOpenDevice={openDevice} />}
          {page === "cables" && <Cables devices={devices} onOpenDevice={openDevice} />}
          {page === "def-pumps" && <Definitions section="pumps" onOpenCables={() => setPage("cables")} />}
          {page === "def-cables" && <Definitions section="cables" onOpenCables={() => setPage("cables")} />}
          {(page.startsWith("guide-") || page.startsWith("info-")) && <Guide id={page} />}
          {page === "settings" && <Settings catalog={catalog} onReloadCatalog={() => reloadCatalog(true)} />}
          {page === "users" && <Users />}
          {page === "releases" && <Releases />}
        </div>
      </main>
      <ConsoleDock device={device} devices={devices} />
      </div>
      {dialogId && devices.some((d) => d.id === dialogId) && (
        <DeviceDialog device={devices.find((d) => d.id === dialogId)!} devices={devices} onClose={() => setDialogId(null)}
                      onChanged={reloadDevices} onLogs={openLogs} onWorkspace={openWorkspace} onOta={openOta} onSheds={openSheds} />
      )}
      {busyMsg && (
        <div className="busy-pop" role="alertdialog" aria-live="assertive" aria-label="Device busy">
          <div className="busy-pop-title">Device busy</div>
          <div className="busy-pop-text">{busyMsg}</div>
          <button className="btn primary" onClick={() => setBusyMsg(null)}>OK</button>
        </div>
      )}
    </div>
  );
}
