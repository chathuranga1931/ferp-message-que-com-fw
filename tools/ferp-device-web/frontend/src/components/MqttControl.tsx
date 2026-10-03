import { useState } from "react";
import { api } from "../api";
import { useLive } from "../store";

const LABEL: Record<string, string> = {
  connected: "Connected", connecting: "Connecting…", reconnecting: "Reconnecting…", disconnected: "Disconnected",
};

export function MqttControl() {
  const mqtt = useLive((s) => s.mqtt);
  const [busy, setBusy] = useState(false);
  if (!mqtt) return <div className="mqtt"><span className="dot" />…</div>;

  const run = (fn: () => Promise<unknown>) => { setBusy(true); fn().finally(() => setBusy(false)); };
  const off = mqtt.state === "disconnected";

  return (
    <div className="mqtt" title={mqtt.error ? `Last error: ${mqtt.error}` : undefined}>
      <span className={`dot ${mqtt.state}`} />
      <span className="mqtt-text">
        <b>{LABEL[mqtt.state]}</b>
        <span className="muted"> {mqtt.host}:{mqtt.port}</span>
      </span>
      {off ? (
        <button className="btn small primary" disabled={busy} onClick={() => run(api.mqttConnect)}>Connect</button>
      ) : (
        <>
          <button className="btn small" disabled={busy} onClick={() => run(api.mqttConnect)} title="Drop and re-open the broker connection">Reconnect</button>
          <button className="btn small" disabled={busy} onClick={() => run(api.mqttDisconnect)}>Disconnect</button>
        </>
      )}
    </div>
  );
}
