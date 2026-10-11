import { useCallback, useEffect, useState } from "react";
import { api, type PermissionDef, type UserAccount } from "../api";
import { useAuth } from "../auth";
import { fmtDateTime } from "../util";

/** Admin: user accounts, their permissions and passwords. */
export default function Users() {
  const { me } = useAuth();
  const [users, setUsers] = useState<UserAccount[]>([]);
  const [perms, setPerms] = useState<PermissionDef[]>([]);
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [form, setForm] = useState({ username: "", password: "", admin: false, permissions: [] as string[] });
  const [pwFor, setPwFor] = useState<{ user: string; pw: string } | null>(null);
  const [confirmDel, setConfirmDel] = useState<string | null>(null);

  const load = useCallback(() => {
    api.users().then(setUsers).catch((e) => setMsg({ ok: false, text: e.message }));
  }, []);
  useEffect(() => { load(); api.permissions().then(setPerms).catch(() => undefined); }, [load]);

  const run = async (fn: () => Promise<unknown>, ok: string) => {
    setMsg(null);
    try { await fn(); setMsg({ ok: true, text: ok }); load(); }
    catch (e) { setMsg({ ok: false, text: (e as Error).message }); }
  };

  const toggle = (list: string[], key: string) => (list.includes(key) ? list.filter((k) => k !== key) : [...list, key]);

  const create = () => run(async () => {
    await api.createUser(form);
    setForm({ username: "", password: "", admin: false, permissions: [] });
  }, `Account ${form.username.trim().toLowerCase()} created`);

  const remove = (u: string) => {
    if (confirmDel !== u) { setConfirmDel(u); return; }
    setConfirmDel(null);
    run(() => api.deleteUser(u), `Account ${u} deleted`);
  };

  return (
    <div className="stack">
      <section className="card">
        <div className="card-head"><h3>Users <span className="muted">({users.length})</span></h3></div>
        <p className="muted small">Everyone who signs in can view the Fleet, Sheds, Cables, the firmware list and OTA progress.
          Tick what else each account may do. Admins can do everything and manage these accounts.</p>
        {msg && <div className={msg.ok ? "ok-text" : "inline-error"}>{msg.text}</div>}
        <div className="table-wrap">
          <table className="list users-table">
            <thead><tr>
              <th>User</th><th>Admin</th>
              {perms.map((p) => <th key={p.key} title={p.description}>{p.label}</th>)}
              <th>Last sign-in</th><th />
            </tr></thead>
            <tbody>
              {users.map((u) => {
                const self = u.username === me?.user;
                return (
                  <tr key={u.username} className={u.disabled ? "disabled-row" : ""}>
                    <td><b>{u.username}</b>{self && <span className="muted small"> (you)</span>}
                      {u.disabled && <div className="inline-error small">disabled</div>}</td>
                    <td className="center"><input type="checkbox" checked={u.admin} disabled={self} aria-label={`${u.username} is admin`}
                      onChange={(e) => run(() => api.updateUser(u.username, { admin: e.target.checked }), `${u.username}: admin ${e.target.checked ? "on" : "off"}`)} /></td>
                    {perms.map((p) => (
                      <td key={p.key} className="center">
                        <input type="checkbox" aria-label={`${u.username}: ${p.label}`} disabled={u.admin}
                               checked={u.admin || u.permissions.includes(p.key)} title={u.admin ? "Admins have every permission" : p.description}
                               onChange={() => run(() => api.updateUser(u.username, { permissions: toggle(u.permissions, p.key) }),
                                 `${u.username}: ${p.label} ${u.permissions.includes(p.key) ? "removed" : "granted"}`)} />
                      </td>
                    ))}
                    <td className="muted small">{u.last_login ? fmtDateTime(u.last_login) : "never"}</td>
                    <td className="row-actions">
                      {pwFor?.user === u.username ? (
                        <span className="row">
                          <input type="password" autoFocus placeholder="New password (8+)" value={pwFor.pw} autoComplete="new-password"
                                 onChange={(e) => setPwFor({ user: u.username, pw: e.target.value })} />
                          <button className="btn small primary" disabled={pwFor.pw.length < 8}
                                  onClick={() => { const pw = pwFor.pw; setPwFor(null); run(() => api.updateUser(u.username, { password: pw }), `${u.username}: password changed`); }}>Save</button>
                          <button className="btn small ghost" onClick={() => setPwFor(null)}>Cancel</button>
                        </span>
                      ) : (
                        <>
                          <button className="btn small" onClick={() => setPwFor({ user: u.username, pw: "" })}>Set password</button>
                          {!self && <button className="btn small ghost" onClick={() => run(() => api.updateUser(u.username, { disabled: !u.disabled }),
                            `${u.username} ${u.disabled ? "enabled" : "disabled"}`)}>{u.disabled ? "Enable" : "Disable"}</button>}
                          {!self && <button className={`btn small ${confirmDel === u.username ? "danger" : "ghost"}`} onClick={() => remove(u.username)}
                                            onBlur={() => setConfirmDel(null)}>{confirmDel === u.username ? "Confirm delete" : "Delete"}</button>}
                        </>
                      )}
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </section>

      <section className="card">
        <div className="card-head"><h3>Add an account</h3></div>
        <div className="form-grid">
          <label className="field"><span>User name</span>
            <input id="new-user" value={form.username} autoComplete="off" onChange={(e) => setForm({ ...form, username: e.target.value })} placeholder="e.g. kasun" /></label>
          <label className="field"><span>Password (8+ characters)</span>
            <input id="new-pass" type="password" value={form.password} autoComplete="new-password" onChange={(e) => setForm({ ...form, password: e.target.value })} /></label>
          <label className="check field-check"><input type="checkbox" checked={form.admin} onChange={(e) => setForm({ ...form, admin: e.target.checked })} /> Admin</label>
        </div>
        {!form.admin && (
          <div className="perm-picks">
            {perms.map((p) => (
              <label key={p.key} className="check" title={p.description}>
                <input type="checkbox" checked={form.permissions.includes(p.key)} onChange={() => setForm({ ...form, permissions: toggle(form.permissions, p.key) })} />
                {p.label} <span className="muted small">— {p.description}</span>
              </label>
            ))}
          </div>
        )}
        <div className="row">
          <button className="btn primary" disabled={!form.username.trim() || form.password.length < 8} onClick={create}>Add account</button>
        </div>
      </section>
    </div>
  );
}
