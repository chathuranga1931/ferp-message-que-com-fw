"""
fake_log_server.py — tiny SFTP server for testing the Cloud logs page without
the real log server. Serves a local folder over SSH with key authentication.

    python devtools/fake_log_server.py --root <folder> --port 2222 --client-pub <key.pub>
    python devtools/fake_log_server.py --make-sample <folder>       # create a sample logs/ tree

Then in Settings → Cloud logs: host 127.0.0.1, port 2222, user "test",
key = the private half of --client-pub, root "logs".
"""

import argparse
import base64
import os
import socket
import threading
import time
from pathlib import Path

import paramiko
from paramiko import SFTPAttributes, SFTPHandle, SFTPServer, SFTPServerInterface, SFTP_OK, SFTP_NO_SUCH_FILE


class _Handle(SFTPHandle):
    def stat(self):
        return SFTPAttributes.from_stat(os.fstat(self.readfile.fileno()))


class _SFTP(SFTPServerInterface):
    ROOT = Path(".")

    def _real(self, path):
        p = (self.ROOT / path.lstrip("/")).resolve()
        if self.ROOT.resolve() not in (p, *p.parents):
            raise PermissionError(path)
        return p

    def list_folder(self, path):
        try:
            p = self._real(path)
            return [SFTPAttributes.from_stat(c.stat(), c.name) for c in p.iterdir()]
        except FileNotFoundError:
            return SFTP_NO_SUCH_FILE

    def stat(self, path):
        try:
            return SFTPAttributes.from_stat(self._real(path).stat())
        except FileNotFoundError:
            return SFTP_NO_SUCH_FILE

    lstat = stat

    def open(self, path, flags, attr):
        try:
            f = open(self._real(path), "rb")
        except FileNotFoundError:
            return SFTP_NO_SUCH_FILE
        h = _Handle(flags)
        h.readfile = f
        return h

    def canonicalize(self, path):
        return "/" + path.lstrip("/")


class _Server(paramiko.ServerInterface):
    def __init__(self, allowed: paramiko.PKey):
        self.allowed = allowed

    def check_auth_publickey(self, username, key):
        return paramiko.AUTH_SUCCESSFUL if key == self.allowed else paramiko.AUTH_FAILED

    def get_allowed_auths(self, username):
        return "publickey"

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED


def serve(root: Path, port: int, client_pub: Path, host_key_path: Path):
    _SFTP.ROOT = root
    if not host_key_path.exists():
        paramiko.RSAKey.generate(2048).write_private_key_file(str(host_key_path))
    host_key = paramiko.RSAKey(filename=str(host_key_path))
    kind, b64 = client_pub.read_text().split()[:2]
    allowed = paramiko.PKey.from_type_string(kind, base64.b64decode(b64))
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", port))
    sock.listen(10)
    print(f"fake SFTP server on 127.0.0.1:{port}, root {root}", flush=True)
    while True:
        conn, _ = sock.accept()

        def handle(c):
            t = paramiko.Transport(c)
            t.add_server_key(host_key)
            t.set_subsystem_handler("sftp", SFTPServer, _SFTP)
            t.start_server(server=_Server(allowed))
            while t.is_active():
                time.sleep(0.5)
        threading.Thread(target=handle, args=(conn,), daemon=True).start()


def make_sample(folder: Path):
    """Create logs/<date>/<SHED>/<SHED>-<PUMP>-<YYYYMMDD>-<HHMM>.txt samples."""
    for date in ("2026-06-08", "2026-06-09"):
        ymd = date.replace("-", "")
        for shed, pumps in (("YAKKALA", ("D04", "P01")), ("BANDA", ("D01",))):
            d = folder / "logs" / date / shed
            d.mkdir(parents=True, exist_ok=True)
            for pump in pumps:
                for hm in ("0815", "1816"):
                    (d / f"{shed}-{pump}-{ymd}-{hm}.txt").write_text(
                        "".join(f"{date} {hm[:2]}:{hm[2:]}:{i:02d} [{shed}/{pump}] sample line {i}\n" for i in range(200)))
    print(f"sample logs written under {folder / 'logs'}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path)
    ap.add_argument("--port", type=int, default=2222)
    ap.add_argument("--client-pub", type=Path)
    ap.add_argument("--host-key", type=Path, default=Path("fake_log_server_host.key"))
    ap.add_argument("--make-sample", type=Path)
    a = ap.parse_args()
    if a.make_sample:
        make_sample(a.make_sample)
    else:
        serve(a.root, a.port, a.client_pub, a.host_key)
