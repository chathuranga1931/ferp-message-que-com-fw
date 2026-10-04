# -*- coding: utf-8 -*-
"""
dump_logs.py - UDP log receiver: one file per device (MAC) per day, under date folders.

Run it with no arguments:

    python3 dump_logs.py

Listens on UDP 22222 for lines "<MAC> : <message>" (same input as log.py, no MAC->name
mapping needed) and writes:

    ~/logs-mac/<YYYY-MM-DD>/<mac>/<mac>-<YYYYMMDD>.txt
        e.g. ~/logs-mac/2026-10-04/48e729331048/48e729331048-20261004.txt

  * <mac> is lower-case without separators (as in the FERP Device Web registry)
  * lines are "[YYYY-MM-DD HH:MM:SS] <message>" in IST (+05:30), like log.py
  * lines that are not "<MAC> : <message>" go to <YYYY-MM-DD>/_invalid/
  * old days are removed by deleting their date folder, e.g. rm -rf ~/logs-mac/2026-09-*

Only one process can listen on the port: stop log.py before starting this.
Optional: --port, --root (default ~/logs-mac), --quiet (no echo to the terminal).

Python 3.6+, standard library only.
"""

import argparse
import datetime
import os
import re
import socket
import sys
import threading
import time

IST = datetime.timezone(datetime.timedelta(hours=5, minutes=30))
MAC_RE = re.compile(r"^[0-9a-f]{12}$")
FLUSH_INTERVAL_S = 2.0           # flush open files at least this often
CLOSE_IDLE_S = 300               # close files of devices that stopped sending


class DailyFiles:
    """Keeps one open append-handle per device; rolls over at local midnight."""

    def __init__(self, root):
        self.root = os.path.abspath(os.path.expanduser(root))
        self.files = {}           # key -> (day, handle, last_write)
        self.lock = threading.Lock()

    def write(self, key, line, now):
        day = now.strftime("%Y%m%d")
        with self.lock:
            cur = self.files.get(key)
            if cur is None or cur[0] != day:
                if cur is not None:
                    cur[1].close()
                folder = os.path.join(self.root, now.strftime("%Y-%m-%d"), key)
                os.makedirs(folder, exist_ok=True)
                fh = open(os.path.join(folder, "%s-%s.txt" % (key, day)), "a", encoding="utf-8")
                cur = (day, fh, now)
            cur[1].write(line + "\n")
            self.files[key] = (cur[0], cur[1], now)

    def maintain(self):
        """Flush everything; close handles idle for CLOSE_IDLE_S."""
        now = datetime.datetime.now(IST)
        with self.lock:
            for key, (day, fh, last) in list(self.files.items()):
                fh.flush()
                if (now - last).total_seconds() > CLOSE_IDLE_S:
                    fh.close()
                    del self.files[key]

    def close_all(self):
        with self.lock:
            for _, fh, _ in self.files.values():
                fh.close()
            self.files.clear()


def parse(message):
    """'48:E7:29:33:10:48 : text' -> ('48e729331048', 'text'); invalid -> (None, message)."""
    try:
        mac, msg = message.split(" : ", 1)
    except ValueError:
        return None, message
    mac = re.sub(r"[^0-9A-Fa-f]", "", mac).lower()
    return (mac, msg) if MAC_RE.match(mac) else (None, message)


def main():
    ap = argparse.ArgumentParser(description="FERP UDP log receiver - one file per device (MAC) per day")
    ap.add_argument("--ip", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=22222)
    ap.add_argument("--root", default="~/logs-mac", help="output folder (default ~/logs-mac)")
    ap.add_argument("--quiet", action="store_true", help="do not echo lines to the terminal")
    args = ap.parse_args()

    files = DailyFiles(args.root)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind((args.ip, args.port))
    except OSError as e:
        print("Failed to bind %s:%d: %s (is log.py still running on this port?)" % (args.ip, args.port, e))
        sys.exit(1)
    sock.settimeout(FLUSH_INTERVAL_S)
    print("Listening on %s:%d - writing to %s" % (args.ip, args.port, files.root), flush=True)

    last_maint = time.time()
    try:
        while True:
            try:
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                data = None
            if data:
                message = data.decode("utf-8", errors="replace").strip()
                now = datetime.datetime.now(IST)
                mac, text = parse(message)
                line = "[%s] %s" % (now.strftime("%Y-%m-%d %H:%M:%S"), text)
                try:
                    files.write(mac or "_invalid", line, now)
                    if not args.quiet:
                        print("%s : %s" % (mac or "INVALID", line), flush=True)
                except Exception as e:
                    print("[ERROR] write failed for %s: %s" % (mac, e), flush=True)
            if time.time() - last_maint >= FLUSH_INTERVAL_S:
                files.maintain()
                last_maint = time.time()
    except KeyboardInterrupt:
        print("\nStopping.")
    finally:
        files.close_all()
        sock.close()


if __name__ == "__main__":
    main()
