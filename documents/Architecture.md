# Architecture — FERP Communication Firmware

## Overview

The FERP communication firmware runs on an **ESP32** and manages a fuel dispenser
terminal: reading Sanki display-tap hardware, uploading transactions to the CubeSphere
cloud, managing WiFi connectivity, logging to SD/SPIFFS, and exposing a real-time-clock.

The entire application is built on the **HSYS message queue framework** (`hsys-framework`
+ `hsys-os` sub-modules).  All inter-module communication uses typed, ref-counted messages
delivered through per-module FreeRTOS task queues.  There are no shared globals, no
callback function pointers between modules, and no direct cross-task function calls.

---

## Repository Layout

```
src/
├── product/
│   ├── app/                        # Shared application core (app.cpp, app.h)
│   │   ├── app_msg_ids.h           # Single source of truth for all message IDs
│   │   ├── app_module_ids.h        # Single source of truth for all module IDs
│   │   └── user_config.h           # Compile-time log flags, HSYS tunables
│   ├── ferp-com-esp32-idf/         # ESP-IDF firmware product
│   │   ├── CMakeLists.txt          # Project-level: IDF include + EXTRA_COMPONENT_DIRS
│   │   └── main/
│   │       ├── CMakeLists.txt      # idf_component_register() — all sources
│   │       └── main.cpp            # app_main() → app_init() → app_run()
│   └── ferp-com-simulator/         # Linux/macOS simulator product
│       ├── build.sh                # CMake + make
│       ├── sim_init.cpp            # Simulator-specific module/task tables
│       └── main/main.cpp           # Opens TCP socket, calls sim_app_init()
├── app-modules/                    # Application business-logic modules
│   ├── app_msg_table.h             # Assembles hsys_msg_desc_t[] for hsys_msg_table_init()
│   ├── ticker/                     # 1 s heartbeat publisher
│   ├── module_sysmon/              # Pool / heap stats reporter
│   ├── module_spiffs/              # SPIFFS mount → MsgSpiffsReady
│   ├── module_sd/                  # SD mount → MsgSdReady / MsgSdStatus
│   ├── module_config/              # Config load/save → MsgConfigReady + domain configs
│   ├── module_timer/               # Software timer slots
│   ├── module_leds/                # Status LEDs
│   ├── module_default_btn/         # Factory-reset button
│   ├── module_print_btn/           # Print buttons (x2)
│   ├── module_buzzer/              # Audio cues
│   ├── module_fuel/                # Sanki 6-digit state machine + nozzle debounce
│   ├── module_wifi/                # WiFi station connection manager
│   ├── module_internet/            # Internet reachability (ICMP ping)
│   ├── module_cloud/               # CubeSphere cloud driver + heartbeat
│   ├── module_timemgr/             # Real-time clock (RTC → NTP → SPIFFS backup)
│   └── module_a/, module_b/        # Reference / example modules
├── app-messages/                   # Typed message classes (msg_*.h / msg_*.cpp)
├── app-pheripherals/               # Application-level peripheral wrappers
├── sub-modules/
│   ├── hsys-framework/             # Core: HsysModule, message bus, pool
│   ├── hsys-os/                    # RTOS abstractions (FreeRTOS / POSIX)
│   ├── pal/                        # Platform Abstraction Layer
│   │   ├── esp-idf/                # ESP-IDF PAL implementations
│   │   └── mac-pc/                 # POSIX PAL implementations + ModuleSimBridge
│   ├── peripheral/                 # hsys_led, hsys_buzzer, hsys_tog_button
│   └── middleware/                 # hsys_config (JSON config store)
└── managed_components/
    └── bblanchon__arduinojson/     # ArduinoJSON (managed component)
```

---

## Build Targets

### ESP-IDF Firmware  (`ferp-com-esp32-idf`)

```
cd src/product/ferp-com-esp32-idf
source '/Users/chathurangadissanayaka/.espressif/tools/activate_idf_v5.5.3.sh'
idf.py build
```

- Toolchain: Xtensa GCC 14.2.0, `-std=gnu++2b`
- Output: `build/ferp-com.bin`
- CMake: 2-file minimum — project `CMakeLists.txt` + `main/CMakeLists.txt`
- All application sources registered in `main/CMakeLists.txt` via `idf_component_register()`

### Simulator  (`ferp-com-simulator`)

```
cd src/product/ferp-com-simulator
sh build.sh && ./build/ferp-com-simulator
```

- Runs on macOS / Linux, POSIX threads replace FreeRTOS tasks
- Exposes a TCP socket on port 9000; `tools/sim-ui/sim_ui.py` is the UI client
- Currently runs: Ticker, ModuleSysmon, ModuleSpiffs, ModuleConfig, ModuleSimBridge

---

## HSYS Framework

### Key Concepts

| Concept | Description |
|---|---|
| `HsysModule` | Base class for every module. Provides `publish()`, `subscribe()`, `log()`. |
| `hsys_msg_t` | Raw message envelope: ID, sender, payload pointer, ref-count. |
| `IHsysMsg` | C++ typed message wrapper. Each message type provides `create()`, `serialize()`, `deserialize()`, `DESCRIPTOR`. |
| `hsys_msg_desc_t` | Static descriptor: message ID, type (NOTIFICATION/DIRECT/BROADCAST), payload size, permissions. |
| `HsysTaskMgr` | Creates FreeRTOS tasks from `hsys_task_desc_t[]`. Each task hosts one or more modules and runs their lifecycle phases. |
| `HsysPool` | Fixed-size block allocator. Pool classes configured in `app.cpp`. |

### Module Lifecycle

Every module executes three phases in strict order before the system starts processing messages:

```
pre_init() → [global barrier] → init() → [global barrier] → post_init() → run loop
```

- `pre_init()` — hardware probe, subscribe to messages
- `init()` — subscribe to messages, request config
- `post_init()` — start timers, kick first action
- Run loop — `on_msg_received()` called for each queued message

### Message Types

| Type | Routing | Use |
|---|---|---|
| `HSYS_MSG_NOTIFICATION` | All subscribers | State change broadcasts (e.g. `MsgSpiffsReady`) |
| `HSYS_MSG_DIRECT` | Addressed to one module | Request/response (e.g. `MsgConfigGetWifi` → `MsgConfigWifi`) |
| `HSYS_MSG_BROADCAST` | All modules | System-wide signals |

---

## Module Registry

| ID | Module | Task | Description |
|---|---|---|---|
| 3 | `Ticker` | `timing_task` | Publishes `MsgTick1000ms` every second |
| 4 | `ModuleSysmon` | `indicator_task` | Logs pool / heap stats |
| 5 | `ModuleSpiffs` | `storage_task` | Mounts SPIFFS; publishes `MsgSpiffsReady` |
| 6 | `ModuleConfig` | `storage_task` | Loads/saves JSON config; publishes `MsgConfigReady` and typed domain configs |
| 7 | `ModuleTimer` | `timing_task` | Software timer slots; delivers `MsgTimerAlarm` DIRECT |
| 8 | `ModuleLeds` | `indicator_task` | Drives status LEDs via PAL |
| 9 | `ModuleDefaultBtn` | `btn_task` | Debounces factory-reset button; publishes `MsgDefaultBtn` |
| 10 | `ModulePrintBtn` | `btn_task` | Debounces print buttons; publishes `MsgPrinterBtn` |
| 11 | `ModuleBuzzer` | `indicator_task` | Drives buzzer via PAL |
| 11 | `ModuleFuel` | `fuel_task` | Runs Sanki 6-digit state machine; publishes `MsgFuelPumped` / `MsgNozzleState` |
| 13 | `ModuleCloud` | `network_task` | CubeSphere HTTPS driver; publishes `MsgCloudStatus` |
| 14 | `ModuleInternet` | `network_task` | ICMP ping; publishes `MsgInternetStatus` |
| 15 | `ModuleWifi` | `network_task` | WiFi STA connection manager; publishes `MsgWifiEvent` |
| 16 | `ModuleSD` | `storage_task` | Mounts SD card; publishes `MsgSdReady` / `MsgSdStatus` |
| 17 | `ModuleTimeMgr` | `timemgr_task` | DS1307 RTC + NTP sync + SPIFFS backup; publishes `MsgTimeStatus` |
| 20 | `ModuleSimBridge` | `sim_bridge_task` | Simulator only — TCP bridge to `sim_ui.py` |

---

## FreeRTOS Task Layout

| Task | Stack | Priority | Modules |
|---|---|---|---|
| `storage_task` | 4096 | 5 | ModuleSpiffs, ModuleSD, ModuleConfig |
| `timing_task` | 2048 | 4 | Ticker, ModuleTimer |
| `indicator_task` | 2048 | 4 | ModuleSysmon, ModuleLeds, ModuleBuzzer |
| `btn_task` | 2048 | 5 | ModulePrintBtn, ModuleDefaultBtn |
| `fuel_task` | 4096 | 5 | ModuleFuel |
| `network_task` | 8192 | 5 | ModuleWifi, ModuleInternet, ModuleCloud |
| `timemgr_task` | 3072 | 5 | ModuleTimeMgr |

> `network_task` uses 8 KB because mbedTLS TLS handshakes alone require 4–6 KB.

---

## Message Taxonomy

### ID Ranges

| Range | Subsystem |
|---|---|
| `0x0001 – 0x00FF` | Sensor / data |
| `0x0100 – 0x010F` | Timer control (start/stop/alarm) |
| `0x0200 – 0x02FF` | System / timing (tick, storage ready, time) |
| `0x0300 – 0x03FF` | Configuration |
| `0x0800 – 0x08FF` | Fuel / dispenser |
| `0x0900 – 0x09FF` | Buttons |
| `0x0A00 – 0x0AFF` | Connectivity (WiFi, internet, cloud) |
| `0x0B00 – 0x0BFF` | *(reserved — Retransmit)* |

### Defined Messages

| ID | Class | Publisher → Subscribers |
|---|---|---|
| `0x0001` | `MsgSensorData` | ModuleA → ModuleB *(demo)* |
| `0x0100` | `MsgTimerStart` | Any → ModuleTimer |
| `0x0101` | `MsgTimerStop` | Any → ModuleTimer |
| `0x0102` | `MsgTimerStartResponse` | ModuleTimer → requester (DIRECT) |
| `0x0103` | `MsgTimerStopResponse` | ModuleTimer → requester (DIRECT) |
| `0x0104` | `MsgTimerAlarm` | ModuleTimer → registered module (DIRECT) |
| `0x0200` | `MsgTick1000ms` | Ticker → all subscribers |
| `0x0201` | `MsgSpiffsReady` | ModuleSpiffs → all |
| `0x0202` | `MsgSdReady` | ModuleSD → all |
| `0x0203` | `MsgSdStatus` | ModuleSD → all |
| `0x0204` | `MsgTimeStatus` | ModuleTimeMgr → all |
| `0x0300` | `MsgConfigReady` | ModuleConfig → all |
| `0x0301` | `MsgConfigSet` | Any → ModuleConfig |
| `0x0302` | `MsgConfigGet` | Any → ModuleConfig |
| `0x0303` | `MsgConfigGetWifi` | Any → ModuleConfig (DIRECT response) |
| `0x0304` | `MsgConfigGetCloud` | Any → ModuleConfig (DIRECT response) |
| `0x0305` | `MsgConfigGetMqtt` | Any → ModuleConfig (DIRECT response) |
| `0x0306` | `MsgConfigGetDT` | Any → ModuleConfig (DIRECT response) |
| `0x0307` | `MsgConfigWifi` | ModuleConfig → ModuleWifi (DIRECT) |
| `0x0308` | `MsgConfigCloud` | ModuleConfig → ModuleCloud (DIRECT) |
| `0x0309` | `MsgConfigMqtt` | ModuleConfig → ModuleMqtt (DIRECT) |
| `0x030A` | `MsgConfigDT` | ModuleConfig → ModuleFuel (DIRECT) |
| `0x0800` | `MsgFuelPumped` | ModuleFuel → ModuleCloud, ModuleRetransmit, ModuleBuzzer |
| `0x0801` | `MsgNozzleState` | ModuleFuel → ModuleLeds, ModuleBuzzer, ModuleCloud |
| `0x0900` | `MsgDefaultBtn` | ModuleDefaultBtn → all |
| `0x0901` | `MsgPrinterBtn` | ModulePrintBtn → ModuleBuzzer |
| `0x0A00` | `MsgWifiEvent` | ModuleWifi → ModuleInternet, ModuleCloud, ModuleLeds |
| `0x0A01` | `MsgInternetStatus` | ModuleInternet → ModuleCloud, ModuleTimeMgr |
| `0x0A02` | `MsgCloudStatus` | ModuleCloud → ModuleLeds, ModuleRetransmit |

---

## Message Flow Diagrams

### Startup Sequence

```
[power on]
     │
     ▼
app_main()
     │  app_init()
     ├─ hsys_pool_init()
     ├─ hsys_module_init()         → all modules register
     ├─ hsys_msg_init() + table    → descriptor table loaded
     └─ hsys_task_mgr_init()       → FreeRTOS tasks created
          │
          ▼ (inside each task)
     pre_init → barrier → init → barrier → post_init → run loop
          │
          ▼
[ModuleSpiffs]  MsgSpiffsReady ──────────────────────────────────────┐
[ModuleConfig]  ← MsgSpiffsReady → load JSON → MsgConfigReady ──────►├─ ModuleWifi
                                                                      ├─ ModuleCloud
                                                                      ├─ ModuleFuel
                                                                      └─ ModuleTimeMgr
```

### Connectivity Stack

```
[WiFi hardware]
      │  PAL events
      ▼
[ModuleWifi] ──── MsgWifiEvent ────────────────────────┐
                  (STA_GOT_IP)                          │
                       │                               ▼
                       ▼                      [ModuleInternet]
               [ModuleInternet]                  ping loop
               ping → connected                       │
                       │                    MsgInternetStatus
                       ▼                      (CONNECTED)
               MsgInternetStatus ──────────────────────┐
                       │                               │
                       ▼                               ▼
               [ModuleTimeMgr]               [ModuleCloud]
               NTP sync                      register + heartbeat
                       │                               │
                       ▼                               ▼
               MsgTimeStatus                  MsgCloudStatus
```

### Fuel Transaction

```
[Sanki display-tap hardware]
      │  UART/GPIO polling inside ModuleFuel task
      ▼
[ModuleFuel] ── internal state machine ──────────────────────────────────┐
      │                                                                   │
      ├── MsgNozzleState (START/STOP) ────────────────────────────────► [ModuleLeds]
      │                                                           └────► [ModuleBuzzer]
      │
      └── MsgFuelPumped (complete transaction) ─────────────────────► [ModuleCloud]
                                                              └──────► [ModuleRetransmit] (future)
```

---

## Configuration System

Config is stored as JSON on SPIFFS. The schema is managed by `hsys_config` middleware.

```
ModuleConfig
   │  On MsgSpiffsReady: load "config.json" into config struct
   │  On MsgConfigSet:   update field, re-save, republish
   │  On MsgConfigGet*:  reply DIRECT to requester
   │
   ├── MsgConfigWifi  → ModuleWifi  (ssid, password, mode)
   ├── MsgConfigCloud → ModuleCloud (endpoint, root CA, device key)
   ├── MsgConfigMqtt  → ModuleMqtt  (broker URI, port, topics)
   └── MsgConfigDT    → ModuleFuel  (display-type, nozzle count, HW rev)
```

---

## Cloud Integration

The cloud connection uses a **dependency-injected driver** (`cloud_driver_t`):

```cpp
// main.cpp (before app_init)
ModuleCloud::instance()->set_driver(cloud_driver_cube_sphere());
```

The `CubeSphereCloudDriver` (`cube_sphere_cloud_driver.cpp`) implements:
- `register_device()` — HTTPS POST with device MAC, firmware version, hardware version
- `on_pumped()` — HTTPS POST with fuel transaction data
- `heartbeat()` — periodic keepalive

> **Known gap**: `set_driver()` is not yet called from `main.cpp`. Cloud module runs in no-op mode until this is wired.

---

## Platform Abstraction Layer (PAL)

All hardware access goes through PAL headers in `src/sub-modules/pal/`:

| PAL Header | ESP-IDF Implementation | Simulator Implementation |
|---|---|---|
| `pal_wifi.h` | `pal_esp_idf_wifi.cpp` | `pal_sim_wifi.cpp` |
| `pal_spiffs.h` | `pal_esp_idf_spiffs.cpp` | `pal_sim_spiffs.cpp` |
| `pal_sd.h` | `pal_esp_idf_sd.cpp` | `pal_sim_sd.cpp` |
| `pal_ntp.h` | `pal_esp_idf_ntp.cpp` | `pal_sim_ntp.cpp` |
| `pal_http_client.h` | `pal_esp_idf_http_client.cpp` | `pal_sim_http_client.cpp` |
| `pal_logger.h` | `pal_esp_idf_logger.cpp` | `pal_sim_logger.cpp` |
| `pal_time.h` | `pal_esp_idf_time.cpp` | `pal_sim_time.cpp` |

---

## Simulator Architecture

The simulator replaces FreeRTOS with POSIX pthreads and POSIX time. It runs a
reduced module set plus `ModuleSimBridge`, which bridges a TCP socket to the
`sim_ui.py` GUI tool.

```
sim_ui.py  (Python/tkinter)
     │  TCP port 9000  JSON frames
     ▼
ModuleSimBridge
     │  injects / observes HSYS messages
     ▼
[Ticker, ModuleSysmon, ModuleSpiffs, ModuleConfig]
```

The simulator intentionally runs a minimal module set to allow isolated testing
of the messaging architecture. The remaining production modules (ModuleWifi,
ModuleCloud, ModuleFuel, etc.) are compiled but not registered in `sim_init.cpp`.

---

## Known Gaps (as of 2026-04-24)

| # | Gap | Location | Priority |
|---|---|---|---|
| 1 | `MsgTimeStatus`, `MsgWifiEvent`, `MsgInternetStatus`, `MsgCloudStatus` not in descriptor table | `app_msg_table.h` | High — runtime allocation will silently fail |
| 2 | `ModuleCloud::set_driver()` never called | `main/main.cpp` | High — cloud is completely inoperative |
| 3 | `fw_version` hardcoded `"1.0.0"` | `module_cloud.cpp:351` | Medium |
| 4 | `info.time_stamp = 0` | `module_cloud.cpp:189` | Medium |
| 5 | `ModuleRetransmit` not implemented | — | Medium — store-and-forward for failed uploads |
| 6 | `ModuleLeds` only subscribes to `MsgSpiffsReady` | `module_leds.cpp` | Low |
| 7 | `ModuleBuzzer` missing nozzle-state and OTA tones | `module_buzzer.cpp` | Low |
| 8 | Log-enable flags missing for most modules | `user_config.h` | Low |
| 9 | Simulator only runs 4 of 17 modules | `sim_init.cpp` | Low |
