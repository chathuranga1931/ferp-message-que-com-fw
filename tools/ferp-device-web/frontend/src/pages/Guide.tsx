/** Guides and Info pages (content to be written). */
export interface GuideSection { title: string; intro: string; topics: string[] }

export const GUIDE_SECTIONS: Record<string, GuideSection> = {
  "guide-troubleshooting": {
    title: "Troubleshooting",
    intro: "Common faults and how to fix them on site or remotely.",
    topics: ["Device offline / not on MQTT", "DT status ERROR (display tap not answering)", "Reset loop after a firmware update",
      "No fuel data from the pump", "SD card errors", "Printer not printing"],
  },
  "guide-installation": {
    title: "Installation",
    intro: "Fitting a device at a fuel station, step by step.",
    topics: ["Before you go: parts and cables", "Mounting and power", "Connecting the display tap", "Wi-Fi and MQTT setup",
      "Setting pump IDs and display type", "Checks before you leave"],
  },
  "guide-ota": {
    title: "Firmware OTA",
    intro: "Updating device and display-tap firmware.",
    topics: ["Version numbers (3.board.x.y)", "OTA from this web app", "Display-tap firmware (boot → part → fw)",
      "HTTP OTA from the old firmware", "Factory flash with esptool", "If an update fails"],
  },
  "guide-devices": {
    title: "Device types",
    intro: "The devices in the fleet and how they differ.",
    topics: ["COM device v1 (board 2308)", "COM device v2 (board 2404)", "COM device v3 (board 2602)",
      "Printer (COM)", "Printer (USB)"],
  },
  "info-pumps": {
    title: "Pump types",
    intro: "Pump makes and models the devices read, with model numbers, photos and wiring details.",
    topics: ["Censtar", "Wayne", "Sanki", "Hongyang", "Longfeng"],
  },
};

export default function Guide({ id }: { id: string }) {
  const s = GUIDE_SECTIONS[id];
  if (!s) return null;
  return (
    <div className="stack">
      <section className="card">
        <div className="card-head"><h3>{s.title}</h3><span className="pill">Not written yet</span></div>
        <p>{s.intro}</p>
        <p className="muted small">Planned topics:</p>
        <ul className="guide-topics">{s.topics.map((t) => <li key={t}>{t}</li>)}</ul>
      </section>
    </div>
  );
}
