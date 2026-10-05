# FERP Device Web

Web version of `tools/ferp-device-tool`, **MQTT only**. It runs as a Windows service on this PC and you reach it from anywhere through Tailscale. One Python process serves the REST API, a live WebSocket and the built React UI on a single port.

```
Browser (tailnet) ──http──▶ <tailscale-ip>:8700  FastAPI
                                         ├─ /        React UI (frontend/dist)
                                         ├─ /api     REST
                                         └─ /ws      live events (console, MQTT status, jobs, OTA)
                                              │
                                              └── one long-lived MQTT connection ──▶ broker ──▶ devices
```

## Features

Everything the desktop tool does over MQTT, plus some fixes:

- **MQTT connection.** Configured in a JSON file, connects automatically at start-up, reconnects by itself, and has Connect / Reconnect / Disconnect buttons in the header.
- **Device info bar.** Read one field or all of them.
- **Message tree and command form.** Built from `src/app-messages/messages/**.json`.
- **Config keys.** Read all, edit values inline, and **Write changes** writes only the keys you edited. The old tool's *Write All* also wrote keys that were never read, as empty values. Each write is checked by reading the key back (✓ / ✗).
- **OTA page.**
  - **Firmware library:** upload `.bdl` files (several at once, or drag and drop), **import from folders** (default: the repo's `releases/`; set others in Settings), download, add notes, delete. Identical bundles are detected.
  - **Flash devices:** pick one or many devices with their online status and current FW (Shed / Pump type / Board filters; ticking is manual). Choose the chunk size and how many at a time, then confirm.
  - **Bundle picker:** each step's bundle is chosen from a radio list filtered by **target** and **version**.
  - **Up to 3 steps in order**, e.g. display tap **boot → part → fw**. **Display-tap set** buttons fill all three when the library has a complete set for a version, and the page warns if the order is wrong.
  - **Between steps** each device pauses (default 15 s, while it reboots), then waits until it answers again (default up to 180 s) before the next step. If a step fails or the device doesn't come back, that device stops and its later steps are skipped.
  - **Activity:** progress per device and a live OTA log for each device; cancel or abort.
  - Scripts can upload too: `curl -F file=@bundle.bdl -F notes="..." http://<host>:8700/api/firmware`.
- **Console dock.** Docked to the bottom of the window on every page and shows all activity (or just the selected device). Filter by level, search, copy, clear. Collapse it (new lines are counted on a badge) and drag its top edge to resize; it remembers its height.
- **Devices page.** Add / edit / delete devices.
  - Each device also has a **device type** (`COM` / `Printer`) plus **shed name, pump ID 1 / 2, pump type, board version and SD card size**.
  - **Type / Shed / Pump type / Board filters** are on the Devices, Fleet and OTA pages.
  - **Settings → OTA → bundle targets per device type** (e.g. `Printer: *printer*, *prn*`) decides which bundles fit which type. A bundle that doesn't fit a selected device's type is refused unless you tick **flash anyway**.
  - **Read from device** fills pump IDs (config `NOZZLE_0_ID` / `NOZZLE_1_ID`) and board version (`HW_VERSION`) from the live device. SD size is entered by hand, because the firmware doesn't report it over MQTT yet.
  - Fleet shows the same fields, and a **Logs** button opens Cloud logs on that device's shed.
- **Settings page.** All app settings are saved to `backend/data/app-config.json`.

New in Phase 2:

- **Fleet page** (the start page). Lists every device heard on the broker, registered or not:
  - online / offline / never seen, last seen, last message, FW version;
  - **Probe** reads the FW version from all or selected devices and shows the reply time;
  - devices that aren't registered can be registered or forgotten.
- **Batch OTA.** Select devices on the Fleet page → **OTA…** opens the OTA page with them pre-selected. One bundle goes to all of them, 1 to 5 at a time, with an option to stop after a failure.
- **Snapshots page.**
  - Save a device's full config as a snapshot, or import / export one as `.json`.
  - Compare a snapshot with any device's last-read values.
  - Apply it to several devices; by default only the keys that differ are written.
  - The config table in the Workspace can also compare against a snapshot and **load its values into the editor** so you can review them before writing.
- **History page.**
  - **Console history:** every console line is saved to SQLite, including lines cleared from the live view. Search by device, level, text and time range; export to `.txt`.
  - **Audit log:** who did what to which device, and when (config writes, sends, OTA, snapshots, settings, devices, firmware, MQTT connect/disconnect). Values of secret keys are masked. Export to `.csv`.
- **Message favourites and recent sends.** Save a built command with ☆ Save; re-send a favourite or a recent command with one click.
- **Config table improvements.**
  - Rows are grouped under headers, with descriptions shown inline.
  - Values are checked before writing: whole numbers, and optional `min` / `max` / `max_len` limits from `config_keys.json`.

New in Phase 4, replacing `tools/cloud-udp-monitor/runssh`:

- **Cloud logs → By device** (default view). Reads the `dump_logs.py` layout (`logs-mac/YYYY-MM-DD/<mac>/<mac>-YYYYMMDD.txt`, see `tools/cloud-udp-monitor/server-script/README.md`). It looks at the most recent 31 day folders by default (Settings → Cloud logs).
  - Device folders are matched with the device list by MAC, so you see label, type and shed.
  - Find a device with search (label / MAC / shed / pump) and the Type / Shed / Pump type / Board filters, then pick a day.
  - Download a zip of all days, or from a chosen date.
  - The **Logs** buttons on Fleet and Devices open the device's folder here.
  - **Live view, like `tail -f`:** a file written to in the last 15 minutes opens in **LIVE** mode automatically. New lines appear about 1–2 s after `dump_logs.py` writes them. The status line shows "last line … ago · checked … ago". At midnight the view moves on to the device's next-day file.
- **Definitions page.** Static reference tables, edited in `frontend/src/definitions.ts`; currently Display Type. The Workspace config table shows the display-type name next to `DISPLAY_TYPE`.
- **Cloud logs page (by date & shed).**
  - Browse the log server by date → shed → pump → file; file names follow `SHED-PUMP-YYYYMMDD-HHMM*.txt`.
  - The viewer opens a file at its end. **Load earlier** pages backwards, **Follow live** works like `tail -f`, and there are a line filter, wrap and auto-scroll.
  - Download one file, or a **zip** of a shed or pump for a day.
  - **Copy link** gives a URL that opens that log (`?log=<date>/<shed>/<file>`).
- **Settings → Cloud logs.**
  - Host, user, **key file on this PC** and optional passphrase, plus a **Test connection** button.
  - Keys can be OpenSSH, traditional PEM or PKCS#8 (Oracle Cloud's `ssh-key-*.key` files are PKCS#8).
  - The server's host key is trusted on first connect and stored in `data/ssh_known_hosts`; if it changes later, connections are refused.
  - A local folder can be used instead of SSH.

Receipt printers (HSYS firmware 44.x COM / 45.x USB, `src/product/ferp-printer-*`):

- **Own MQTT topic tree.** Printers publish under `ferp/ferp-printer/...`. `mqtt.dev_types` in the config maps a device type to its tree (default `{"Printer": "ferp-printer"}`); the app listens on every tree and addresses each device by its type.
- **Config keys per device type.** `config_keys.json` groups and keys can carry `"device_types"`. The Workspace shows only the keys a device has: printers get **Receipt** and **Cloud print**, COM units keep Cloud, Hardware, Printer client and Features. **Read all** reads only those keys.
- **Printer panel** (Workspace, Printer devices only):
  - **Status:** printer ready, transport, bill count and job counters, plus cloud print state, queue, polls, printed, failed and rejected.
  - **Actions:** print sample, print info slip, beep, and reset the bill count (asks first).
  - **Test receipt:** a receipt or totalizer slip with your own values.
  - **Preview:** a live preview of the receipt with the printer's current settings (themes 1 to 3, column width, cut, thank-you lines). Read the config first.
- **`POST /api/devices/{id}/request`** sends a command and waits for one named reply (`{"msg", "data", "expect", "timeout"}`). The printer panel uses it; scripts can too.
- `devtools/fake_device.py --printer --id simprn01` simulates a printer for testing.

Long reads and writes run on the server, so they keep going if you close the browser. Every open tab sees the same live state.

## Layout

```
tools/
├── ferp-core/                 shared protocol package (also used by ferp-device-tool)
└── ferp-device-web/
    ├── backend/
    │   ├── app/
    │   │   ├── ports.py       interfaces: ConfigStore, DeviceRepository, BlobStore, EventBus,
    │   │   │                  AuthProvider, DocumentStore, HistoryStore
    │   │   ├── adapters/      local.py (JSON files, folder, in-process bus) · sqlite_history.py · auth.py
    │   │   │                  · log_sources.py (SSH/SFTP or folder)
    │   │   ├── services/      mqtt_hub · device_ops · fleet · ota (+ batch) · snapshots · favorites · cloud_logs
    │   │   │                  · audit · console · catalog
    │   │   ├── container.py   picks adapters (FERP_STORAGE, FERP_AUTH_MODE)
    │   │   ├── api/           REST by area (system, devices, ota, fleet, history) + ws.py
    │   │   └── main.py        app factory; __main__.py = `python -m app`
    │   ├── config/            app-config.default.json, devices.seed.json (first-run defaults)
    │   ├── devtools/          fake_device.py (MQTT device simulator) · fake_log_server.py (SFTP log server)
    │   └── data/              runtime state (git-ignored): app-config.json, devices.json, blobs/,
    │                          docs/ (snapshots, favourites, presence), history.db
    ├── frontend/              React + TypeScript + Vite
    └── deploy/windows/        install-service.ps1, uninstall-service.ps1, run-dev.ps1
```

Message definitions are read live from `src/app-messages/messages` while this folder is inside the firmware repo. Once moved to another repo, put a synced copy in `backend/messages-json/` or set `FERP_MESSAGES_DIR`. Config keys come from `ferp-core/ferp_core/data/config_keys.json`.

## Install as a service (start at power-on)

From an **elevated** PowerShell at the repo root:

```powershell
powershell -ExecutionPolicy Bypass -File tools\ferp-device-web\deploy\windows\install-service.ps1
```

What it does:
- Creates the virtualenv and installs the Python packages.
- Builds the UI.
- Registers the `ferp-device-web` service with [WinSW](https://github.com/winsw/winsw):
  - starts automatically at boot, before anyone logs in,
  - restarts if it crashes,
  - starts after Mosquitto,
  - writes rotating logs to `deploy/windows/logs`.
- Listens on all interfaces and adds a firewall rule that allows port 8700 **only from Tailscale addresses** (`100.64.0.0/10`). Open `http://<tailscale-ip>:8700` from any device on your tailnet, or `http://localhost:8700` on this PC. No Tailscale configuration is needed.

Re-run the same command after pulling new code to update. The installer also gives your Windows user permission to start and stop the service, so later updates only need:

```powershell
tools\ferp-device-web\deploy\windows\restart-service.ps1          # restart + wait until it answers
tools\ferp-device-web\deploy\windows\restart-service.ps1 -Build   # rebuild the UI first (-Deps: update Python packages)
```

Or `Restart-Service ferp-device-web`, or Settings → Restart server.

If the UI and the server get out of step (new code on disk, old process still running), the app shows a red banner asking for a restart. Unknown API calls return a clear 404 instead of "Method Not Allowed".

Notes:
- Options: `-BindHost 127.0.0.1` for this PC only; `-AllowFrom 100.64.0.0/10,LocalSubnet` to also allow the LAN. There's no login yet, so think before opening it wider.
- The service must not use the Microsoft Store Python. The script picks a python.org / `py install` interpreter, or you can pass `-Python <path>`.
- Day-to-day: `Restart-Service ferp-device-web`. To remove: `uninstall-service.ps1` (also removes the firewall rule).

## Development

```powershell
tools\ferp-device-web\deploy\windows\run-dev.ps1     # backend :8701 (data-dev/) + Vite hot reload on :5173
python backend\devtools\fake_device.py --id sim001   # then add a device with MAC "sim001"
python backend\devtools\import_devices_xlsx.py FERP-Device-List.xlsx --dry-run   # add new devices from the spreadsheet (drop --dry-run to write)
python backend\devtools\fake_log_server.py --make-sample <dir>                                   # sample logs tree
python backend\devtools\fake_log_server.py --root <dir> --port 2222 --client-pub <key.pub>      # SFTP on 127.0.0.1:2222
```

The API docs are at `/docs`.

## Configuration

| Where | What |
|---|---|
| Settings page → `backend/data/app-config.json` | MQTT broker host/port/credentials, auto-connect, dev_type, broker presets, response timeout, write verification, OTA chunk size, console buffer, fleet online window / probe / auto-probe, history retention |
| `backend/.env` (see `.env.example`) | Deployment settings: bind host/port, data dir, `FERP_AUTH_MODE`, `FERP_STORAGE` |

## Moving to the cloud later

Services depend only on the interfaces in `app/ports.py`. To run on Google Cloud or AWS:
1. Add adapters, for example Firestore/DynamoDB for config, devices and documents, GCS/S3 for firmware, Pub/Sub or Redis for events, BigQuery or Cloud SQL for history.
2. Select them in `container.py` with `FERP_STORAGE=gcp|aws`.
3. Run the same app in a container. Keep a single instance (`min-instances=1`): the MQTT connection, jobs and OTA sessions live in the process.

**Auth.** `FERP_AUTH_MODE=none` is the default; the firewall rule limits access to your tailnet. `FERP_AUTH_MODE=tailscale` only works behind `tailscale serve`, which adds the `Tailscale-User-Login` header. A password or OIDC login can be added later as another `AuthProvider`.

## Roadmap

1. ✅ Skeleton and Windows service
2. ✅ Backend with everything the desktop tool does over MQTT
3. ✅ Frontend with the same
4. ✅ New features: fleet dashboard, batch OTA, config snapshots, saved console history, audit log, message favourites, config table improvements
5. ✅ Cloud logs (replaces `cloud-udp-monitor`): browse, follow and download logs from the server over SSH
6. Docker image, cloud adapters and login (moved to last)
