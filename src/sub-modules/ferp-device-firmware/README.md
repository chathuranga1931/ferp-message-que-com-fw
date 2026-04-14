# FERP Device Firmware – Naming, Versioning & Tagging Reference

## Repository Folder Structure

```
ferp-device-firmware/
├── common/
│   ├── core/              # Core utilities (logging, errors, state machines)
│   ├── protocol/          # Shared protocol definitions (COM ↔ Printer ↔ ESP07)
│   ├── drivers/           # Reusable drivers (UART, SPI, GPIO abstractions)
│   ├── services/          # Shared services (config, storage, watchdog, etc.)
│   └── utils/             # Helpers, macros, version metadata
│
├── ferp-com-app/
│   ├── targets/
│   │   ├── 2308-modified/
│   │   ├── 2404/
│   │   └── 2407/
│   └── app/               # COM application-specific logic
│
├── ferp-com-production/
│   ├── targets/
│   │   ├── 2308-modified/
│   │   ├── 2404/
│   │   └── 2407/
│   └── tests/             # Manufacturing / production tests
│
├── ferp-printer/
│   ├── targets/
│   │   ├── 2308/
│   │   ├── 2404/
│   │   └── 2407/
│   └── app/               # Printer-specific application logic
│
├── ferp-dt-esp07/
│   └── targets/           # ESP07 communication firmware
│
├── ferp-dt-esp32-adapter/
│   └── targets/           # ESP32 adapter board firmware
│
└── docs/
    └── versioning.md      # This document
```

## Firmware Targets

| Firmware Name            | Folder Name              | Purpose                                                        |
| ------------------------ | ------------------------ | -------------------------------------------------------------- |
| FERP COM Application     | `ferp-com-app/`          | Main application firmware for COM board (field / OTA deployed) |
| FERP COM Production Test | `ferp-com-production/`   | Manufacturing and production test firmware (factory use only)  |
| FERP Printer             | `ferp-printer/`          | Printer module firmware (ESP32-based)                          |
| FERP DT ESP07            | `ferp-dt-esp07/`         | ESP07 communication firmware                                   |
| FERP DT ESP32 Adapter    | `ferp-dt-esp32-adapter/` | ESP32 adapter board firmware                                   |

---

## Versioning Scheme (Common to All Firmware)

| Level | Name | Meaning                     | Typical Reasons                                                       |
| ----- | ---- | --------------------------- | --------------------------------------------------------------------- |
| MAJOR | X    | Breaking / unsafe upgrade   | Protocol breaking change, NVS/partition change, incompatible behavior |
| MINOR | Y    | Backward-compatible feature | New feature, new PCB support, protocol extension                      |
| PATCH | Z    | Bug fix / internal change   | Logic fix, stability improvement, refactor                            |

Format:

```
MAJOR.MINOR.PATCH
```

Example:

```
1.2.3
```

---

## Git Tag Naming & Binary Naming

| Firmware              | Tag Prefix | Tag Example      | Binary Name Example                         |
| --------------------- | ---------- | ---------------- | ------------------------------------------- |
| ferp-com-app          | `v`        | `v1.2.3`         | `ferp-com-app-esp32-pcbA-v1.2.3.bin`        |
| ferp-com-production   | `pt-`      | `pt-v1.3.0`      | `ferp-com-prodtest-esp32-pcbA-PT-1.3.0.bin` |
| ferp-printer          | `printer-` | `printer-v1.1.0` | `ferp-printer-esp32-pcbB-v1.1.0.bin`        |
| ferp-dt-esp07         | `esp07-`   | `esp07-v1.1.4`   | `ferp-dt-esp07-pcbA-v1.1.4.bin`             |
| ferp-dt-esp32-adapter | `adapter-` | `adapter-v1.0.2` | `ferp-dt-esp32-adapter-pcbA-v1.0.2.bin`     |

---|---|---|
| ferp-com-app | `v` | `v1.2.3` |
| ferp-com-production | `pt-` | `pt-v1.3.0` |
| ferp-printer | `printer-` | `printer-v1.1.0` |
| ferp-dt-esp07 | `esp07-` | `esp07-v1.1.4` |
| ferp-dt-esp32-adapter | `adapter-` | `adapter-v1.0.2` |

---

|---|---|
| ferp-com-app | `ferp-com-app-esp32-<pcb>-vX.Y.Z.bin` | `ferp-com-app-esp32-pcbA-v1.2.3.bin` |
| ferp-com-production | `ferp-com-prodtest-esp32-<pcb>-PT-X.Y.Z.bin` | `ferp-com-prodtest-esp32-pcbA-PT-1.3.0.bin` |
| ferp-printer | `ferp-printer-esp32-<pcb>-vX.Y.Z.bin` | `ferp-printer-esp32-pcbB-v1.1.0.bin` |
| ferp-dt-esp07 | `ferp-dt-esp07-<pcb>-vX.Y.Z.bin` | `ferp-dt-esp07-pcbA-v1.1.4.bin` |
| ferp-dt-esp32-adapter | `ferp-dt-esp32-adapter-<pcb>-vX.Y.Z.bin` | `ferp-dt-esp32-adapter-pcbA-v1.0.2.bin` |

---

## Versioning Rules Summary

| Rule                                                 | Applies To                 |
| ---------------------------------------------------- | -------------------------- |
| Binary change requires version bump                  | All firmware               |
| Protocol versions are NOT in tags                    | All firmware               |
| Production-test firmware uses separate tag namespace | ferp-com-production        |
| Different PCBs require different binaries            | All hardware targets       |
| Compatibility is enforced at runtime, not by tag     | All communicating firmware |
