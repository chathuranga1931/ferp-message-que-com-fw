# UDP log receivers (run on the log server)

| Script | Layout | Notes |
|---|---|---|
| `log.py` | `./YYYY-MM-DD/<SHED>/<SHED>-<PUMP>-YYYYMMDD-HHMM.txt` | Original. Needs `MAC_MAPPING` for every device; unknown MACs go to `UNKNOWN`. New file every 30 min. |
| `dump_logs.py` | `~/logs-mac/YYYY-MM-DD/<mac>/<mac>-YYYYMMDD.txt` | No mapping. One file per device per day, under date folders. `<mac>` is lowercase without separators, the same form FERP Device Web uses. |

Both listen on UDP **22222** and take lines `<MAC> : <message>`. **Only one can run at a time**, because they share the port.

## Run

```bash
python3 dump_logs.py
```

No arguments are needed; it writes to `~/logs-mac` from wherever you start it. Optional: `--quiet` (don't echo lines), `--root <dir>`, `--port <n>`.

Remove old days by deleting their date folder, e.g. `rm -rf ~/logs-mac/2026-09-*`.

The web app's **Cloud logs → By device** view reads `logs-mac/`, and **By date & shed** reads `logs/`. Both folder names can be changed in Settings → Cloud logs; they're relative to the SSH user's home.

## Keep it running (systemd, optional)

```ini
# /etc/systemd/system/ferp-udp-log.service
[Unit]
Description=FERP UDP log receiver (per device)
After=network-online.target

[Service]
User=ubuntu
ExecStart=/usr/bin/python3 /home/ubuntu/logs/dump_logs.py --quiet
Restart=always

[Install]
WantedBy=multi-user.target
```

`sudo systemctl daemon-reload && sudo systemctl enable --now ferp-udp-log`
