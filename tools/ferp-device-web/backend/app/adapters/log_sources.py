"""
LogSource adapters: SFTP over SSH (the cloud log server) and a local folder.

SSH host keys use trust-on-first-use: the server's key is stored in
<data>/ssh_known_hosts on the first connection; a different key later is refused
(paramiko raises BadHostKeyException) until that file entry is removed.
"""

import stat as stat_mod
import threading
from pathlib import Path, PurePosixPath
from typing import Optional


def _clean(rel: str) -> str:
    """Normalise a relative path and refuse anything escaping the root."""
    parts = [p for p in PurePosixPath(rel.replace("\\", "/")).parts if p not in ("", ".")]
    if any(p == ".." or p.startswith("/") for p in parts):
        raise ValueError(f"Bad path: {rel!r}")
    return "/".join(parts)


class LocalLogSource:
    def __init__(self, root: Path):
        self._root = root.resolve()

    def _p(self, rel: str) -> Path:
        p = (self._root / _clean(rel)).resolve()
        if p != self._root and self._root not in p.parents:
            raise ValueError(f"Bad path: {rel!r}")
        return p

    def listdir(self, rel: str) -> list[dict]:
        p = self._p(rel)
        if not p.is_dir():
            raise FileNotFoundError(rel)
        out = []
        for c in p.iterdir():
            st = c.stat()
            out.append({"name": c.name, "is_dir": c.is_dir(), "size": st.st_size, "mtime": st.st_mtime})
        return out

    def stat(self, rel: str) -> dict:
        st = self._p(rel).stat()
        return {"size": st.st_size, "mtime": st.st_mtime}

    def read(self, rel: str, offset: int, length: int) -> bytes:
        with open(self._p(rel), "rb") as fh:
            fh.seek(max(0, offset))
            return fh.read(max(0, length))

    def close(self) -> None:
        pass


def load_private_key(path: str, passphrase: Optional[str]):
    """Load an SSH private key of any common format, with clear errors.

    Parsed with `cryptography` (OpenSSH, traditional PEM and PKCS#8 — e.g. Oracle
    Cloud's generated ssh-key-*.key, which paramiko alone cannot read), then handed
    to paramiko."""
    import io
    import paramiko
    from cryptography.hazmat.primitives import serialization as ser
    from cryptography.hazmat.primitives.asymmetric import ec, ed25519, rsa

    p = Path(path)
    if not p.is_file():
        raise ConnectionError(f"SSH key file not found: {path}")
    data = p.read_bytes()
    pwb = passphrase.encode() if passphrase else None
    load = ser.load_ssh_private_key if b"BEGIN OPENSSH PRIVATE KEY" in data else ser.load_pem_private_key
    try:
        try:
            key = load(data, password=pwb)
        except TypeError:
            if pwb is None:
                raise ConnectionError("The SSH key is encrypted — enter its passphrase (Settings → Cloud logs)")
            key = load(data, password=None)          # passphrase given but the key is not encrypted
    except ValueError as exc:
        raise ConnectionError(f"Could not read SSH key {path} (wrong passphrase or unsupported format): {exc}")
    openssh = key.private_bytes(ser.Encoding.PEM, ser.PrivateFormat.OpenSSH, ser.NoEncryption()).decode()
    cls = (paramiko.RSAKey if isinstance(key, rsa.RSAPrivateKey) else
           paramiko.Ed25519Key if isinstance(key, ed25519.Ed25519PrivateKey) else
           paramiko.ECDSAKey if isinstance(key, ec.EllipticCurvePrivateKey) else None)
    if cls is None:
        raise ConnectionError(f"Unsupported SSH key type in {path}")
    return cls(file_obj=io.StringIO(openssh))


class SshLogSource:
    """One persistent SFTP session, re-opened automatically after a drop."""

    def __init__(self, host: str, port: int, username: str, key_path: str, passphrase: str,
                 root: str, known_hosts: Path, timeout: float = 10.0):
        self._args = (host, port, username, key_path, passphrase or None)
        self._root = root.strip("/") or "."
        self._known_hosts = known_hosts
        self._timeout = timeout
        self._lock = threading.RLock()
        self._client = None
        self._sftp = None

    # ── connection ────────────────────────────────────────────────────────────

    def _connect(self):
        import paramiko
        host, port, username, key_path, passphrase = self._args
        if not key_path:
            raise ConnectionError("No SSH key file configured (Settings → Cloud logs)")
        pkey = load_private_key(key_path, passphrase)
        client = paramiko.SSHClient()
        if self._known_hosts.exists():
            client.load_host_keys(str(self._known_hosts))
        client.set_missing_host_key_policy(paramiko.AutoAddPolicy())   # first use only; mismatch still fails
        try:
            client.connect(host, port=port, username=username, pkey=pkey,
                           timeout=self._timeout, banner_timeout=self._timeout, auth_timeout=self._timeout,
                           look_for_keys=False, allow_agent=False)
        except paramiko.BadHostKeyException as exc:
            raise ConnectionError(f"Server host key changed for {host} — if expected, remove its line from "
                                  f"{self._known_hosts}") from exc
        except paramiko.AuthenticationException as exc:
            raise ConnectionError(f"SSH authentication failed for {username}@{host} with {key_path}") from exc
        except (OSError, paramiko.SSHException) as exc:
            raise ConnectionError(f"SSH connection to {host}:{port} failed: {exc}") from exc
        self._known_hosts.parent.mkdir(parents=True, exist_ok=True)
        client.save_host_keys(str(self._known_hosts))
        self._client, self._sftp = client, client.open_sftp()
        self._sftp.get_channel().settimeout(self._timeout * 3)

    def _call(self, fn):
        """Run fn(sftp) with one reconnect attempt on a dropped session."""
        with self._lock:
            for attempt in (1, 2):
                if self._sftp is None:
                    self._connect()
                try:
                    return fn(self._sftp)
                except (FileNotFoundError, PermissionError):
                    raise
                except (OSError, EOFError) as exc:
                    if isinstance(exc, IOError) and getattr(exc, "errno", None) == 2:
                        raise FileNotFoundError(str(exc)) from exc
                    self.close()
                    if attempt == 2:
                        raise ConnectionError(f"SFTP error: {exc}") from exc

    def _abs(self, rel: str) -> str:
        r = _clean(rel)
        return f"{self._root}/{r}" if r else self._root

    # ── LogSource ─────────────────────────────────────────────────────────────

    def listdir(self, rel: str) -> list[dict]:
        def go(sftp):
            return [{"name": a.filename, "is_dir": stat_mod.S_ISDIR(a.st_mode or 0),
                     "size": a.st_size or 0, "mtime": a.st_mtime or 0}
                    for a in sftp.listdir_attr(self._abs(rel))]
        return self._call(go)

    def stat(self, rel: str) -> dict:
        def go(sftp):
            a = sftp.stat(self._abs(rel))
            return {"size": a.st_size or 0, "mtime": a.st_mtime or 0}
        return self._call(go)

    def read(self, rel: str, offset: int, length: int) -> bytes:
        def go(sftp):
            with sftp.open(self._abs(rel), "rb") as fh:
                fh.seek(max(0, offset))
                return fh.read(max(0, length))
        return self._call(go)

    def close(self) -> None:
        with self._lock:
            for obj in (self._sftp, self._client):
                try:
                    if obj is not None:
                        obj.close()
                except Exception:
                    pass
            self._client = self._sftp = None


def make_source(cfg, data_dir: Path) -> Optional[object]:
    """Build the LogSource for a LogsConfig."""
    if cfg.source == "local":
        if not cfg.local_root:
            raise ConnectionError("No local logs folder configured (Settings → Cloud logs)")
        return LocalLogSource(Path(cfg.local_root))
    return SshLogSource(cfg.host, cfg.port, cfg.username, cfg.key_path, cfg.key_passphrase, cfg.root,
                        data_dir / "ssh_known_hosts")
