import { useState } from "react";
import { api } from "../api";

/** Sign-in screen shown when the server needs a login. */
export default function Login() {
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true); setErr(null);
    try {
      await api.login(username.trim(), password);
      location.reload();
    } catch (ex) {
      setErr((ex as Error).message);
      setBusy(false);
    }
  };

  return (
    <div className="login-wrap">
      <form className="card login-card" onSubmit={submit}>
        <div className="brand">FERP <span>Device Web</span></div>
        <h2>Sign in</h2>
        <label className="field"><span>User name</span>
          <input id="login-user" autoFocus autoComplete="username" value={username} onChange={(e) => setUsername(e.target.value)} /></label>
        <label className="field"><span>Password</span>
          <input id="login-pass" type="password" autoComplete="current-password" value={password} onChange={(e) => setPassword(e.target.value)} /></label>
        {err && <div className="inline-error">{err}</div>}
        <button className="btn primary" type="submit" disabled={busy || !username.trim() || !password}>{busy ? "Signing in…" : "Sign in"}</button>
        <p className="muted small">Ask an admin for an account or a new password.</p>
      </form>
    </div>
  );
}
