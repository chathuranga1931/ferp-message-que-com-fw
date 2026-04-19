# Porting Plan — Old Event-Based App → HSYS Message Queue Architecture

## Progress legend
- ✅ **DONE** — built, verified in simulator
- 🔶 **PARTIAL** — core done, some sub-tasks remain
- ⬜ **PENDING** — not yet started

---

## Philosophy

- **One module per sprint.** Each sprint ends with the simulator running and the
  Python UI showing the correct visual state for that module.
- **Stub-first.** Every new module is created as a stub (compiles, publishes
  nothing, subscribes to nothing) before real logic is added. This keeps the
  build green at every commit.
- **Test at the task boundary.** The simulator + Python UI is the test bench.
  Real hardware is only needed for things the simulator physically cannot do
  (UART tap, SD card, actual WiFi RF).
- **No regressions.** `ticker` and `sysmon` stay in the build.
  `module_a` and `module_b` are disabled (commented out in `app.cpp`).

---

## Layer diagram

```
┌─────────────────────────────────────────────────────────────────┐
│  Python UI  (TCP socket, localhost:9000)                        │
│  • LED circles  • nozzle state  • WiFi/internet bars            │
│  • OTA progress  • MQTT log  • cloud heartbeat counter          │
│  • Message inject widget  (SIM_MSG_INJECT → firmware bus)       │
└───────────────────────┬─────────────────────────────────────────┘
                        │  JSON lines  (TCP duplex, port 9000)
┌───────────────────────▼─────────────────────────────────────────┐
│  ferp-com-simulator  (macOS native C++17 binary)                │
│                                                                 │
│  ModuleSimBridge  ← serialises app messages → Python UI         │
│  sim_msg_inject   ← deserialises SIM_MSG_INJECT → HSYS bus      │
│                                                                 │
│  [All real app modules running in their tasks]                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Step 0 — Infrastructure: SimBridge + Python UI skeleton  ✅ DONE

### 0-A  `ModuleSimBridge` + `MacDriver` (C++) ✅
- `src/sub-modules/pal/mac-pc/driver/module_sim_bridge.h/.cpp` — HSYS module that owns TCP I/O
- `src/sub-modules/pal/mac-pc/driver/mac_driver.h/.cpp` — TCP engine (port 9000)
- Registered in the simulator's module table via `pal_mac_system.cpp`

### 0-B  Python UI (`tools/sim-ui/`) ✅
```
tools/sim-ui/
    sim_ui.py                          ← main window, TCP client on localhost:9000
    widgets/
        led_widget.py
        nozzle_widget.py
        log_widget.py
        config_widget.py
        message_inject_widget.py       ← send SIM_MSG_INJECT commands to firmware
    README.md
```

Launch:
```bash
# Terminal 1 — simulator binary
cd src/product/ferp-com-simulator && ./build/ferp-com-simulator

# Terminal 2 — Python UI
cd tools/sim-ui && python3 sim_ui.py --port 9000
```

### 0-C  Shared `app.cpp` architecture ✅
- `src/product/app/app.cpp` — fully shared between simulator and ESP32-IDF
- `src/product/ferp-com-simulator/main/main.cpp` — thin platform wrapper
  (`app_platform_pre_init` override + `app_run` nanosleep loop)
- `src/product/ferp-com-esp32-idf/main/main.cpp` — `app_init()` + `while(true){app_run()}` only

**✅ Gate: Simulator builds, Python UI connects, tick heartbeat visible.**

---

## Step 0-D — Shared timer service ✅ DONE

`ModuleTimer` is a shared infrastructure module (not simulator-only):

- `src/app-modules/module_timer/module_timer.h/.cpp`
- 20 timer slots, 100 ms tick resolution
- Messages: `MsgTimerStart`, `MsgTimerStop`, `MsgTimerStartResponse`,
  `MsgTimerStopResponse`, `MsgTimerAlarm` (IDs `0x0100`–`0x0104`)
- Any module can request a one-shot or repetitive timer via DIRECT message
- **Bug fixed**: `ModuleTimer::_tick_cb` now calls `hsys_timer_get_userdata(handle)`
  (FreeRTOS/PAL handle-passing convention) instead of casting the handle directly.
  Without this fix the `MacOsTimer` struct was misread as `ModuleTimer`, causing
  garbage slot data, pool exhaustion, and a segfault ~1.6 s after startup.

---

## Step 0-E — Message JSON descriptors + message inject infrastructure ✅ DONE

### JSON message descriptors ✅
All 13 message descriptor files live alongside the C++ sources:
```
src/app-messages/messages/
    Timer/
        msg_timer_start.json
        msg_timer_stop.json
        msg_timer_start_response.json
        msg_timer_stop_response.json
        msg_timer_alarm.json
    Config/
        msg_config_set.json
        msg_config_get.json
        msg_config_ready.json
    System/
        msg_tick_1000ms.json
        msg_spiffs_ready.json
        msg_sensor_data.json
    Button/
        msg_default_btn.json
        msg_printer_btn.json
src/app-modules/modules.json          ← module ID registry for Python UI
```

### Firmware `from_json()` deserialisation ✅
All 13 message classes have a simulator-only static factory method:
```cpp
#ifdef FERP_SIMULATOR
    static hsys_msg_t *from_json(const char *payload_json, hsys_module_id_t sender_id);
#endif
```
Implemented in each `.cpp` using ArduinoJson v7. Special cases:
- `MsgConfigSet` — union value field handled with a switch on `type`
- Zero-payload messages — `from_json()` calls `create(sender_id)` directly

### `sim_msg_inject` driver ✅
- `src/sub-modules/pal/mac-pc/driver/sim_msg_inject.h/.cpp`
- Parses the outer `SIM_MSG_INJECT` JSON envelope, dispatches to the correct
  `MsgXxx::from_json()`, then calls `publish()` (NOTIFICATION) or `send()` (DIRECT)
- Wired into `mac_driver.cpp` alongside existing `SIM_GPIO_OUT` handling
- `FERP_SIMULATOR=1` defined on both `pal_mac` and `app_messages` CMake targets

### Python `message_inject_widget.py` ✅
- Reads JSON descriptors from `src/app-messages/messages/`
- Reads module list from `src/app-modules/modules.json`
- Dropdown to select message type, editable fields for payload, sends `SIM_MSG_INJECT` over TCP

---

## Sprint 1 — Config module  🔶 PARTIAL

### What is done ✅
- `src/app-modules/module_config/module_config.h/.cpp` — full implementation
- `src/app-messages/msg_config_ready.h/.cpp` (ID `0x0300`) — published when config loaded/updated
- `src/app-messages/msg_config_set.h/.cpp` (ID `0x0301`) — set one config field by key/value
- `src/app-messages/msg_config_get.h/.cpp` (ID `0x0302`) — request re-publish of current config
- `src/product/ferp-com-simulator/SPIFFS/spiffs/Configs/DeviceConfigs.json` — config on SPIFFS emulation

### Config flow
ModuleSpiffs mounts → publishes `MsgSpiffsReady` → ModuleConfig reads JSON from SPIFFS →
publishes `MsgConfigReady` with full `app_config_t` snapshot.

### What remains ⬜
- `MsgConfigResetRequest` (factory-reset to defaults) — not yet implemented
- Python UI "Config loaded" badge/panel (`config_widget.py` exists but bridge subscription is limited)

---

## Sprint 2 — WiFi module  ⬜ PENDING

### Files to create
```
src/app-modules/module_wifi/
    module_wifi.h
    module_wifi.cpp
src/app-messages/
    msg_wifi_event.h / .cpp   (ID 0x0400)
```

### Simulator stub
`src/product/ferp-com-simulator/sim_wifi/sim_wifi_module.cpp`
Subscribes to `MsgConfigReady`, then after a configurable delay (e.g. 2 s)
publishes a fake `MsgWifiEvent(STA_GOT_IP, ip=192.168.1.100)`.

### Python UI widget
- WiFi signal bar widget (0–4 bars)
- Shows "STA_CONNECTED", "GOT_IP 192.168.1.100", "DISCONNECTED"

### Test
1. Simulator starts → Config loads → WiFi "connects" after 2 s → Python shows bars
2. Inject `MsgWifiEvent(DISCONNECTED)` via message inject widget → bars drop to 0

---

## Sprint 3 — Internet module  ⬜ PENDING

### Files to create
```
src/app-modules/module_internet/
    module_internet.h
    module_internet.cpp
src/app-messages/
    msg_internet_status.h / .cpp   (ID 0x0500)
```

### Simulator stub
Subscribes to `MsgWifiEvent(GOT_IP)`, publishes `MsgInternetStatus(connected=true)`
after a 1 s delay. No actual HTTP ping needed in simulator.

### Python UI widget
Globe icon: grey = disconnected, green = connected.

---

## Sprint 4 — Fuel / DispTap modules  *(the core domain)*  ⬜ PENDING

> This is the most important module to test in the simulator because it has
> the most complex state machine and it is impossible to test without hardware
> **unless** a simulator stub injects fake display-tap frames.

### Files to create
```
src/app-modules/module_fuel/
    module_fuel.h
    module_fuel.cpp
src/app-modules/module_disptap/
    module_disptap.h
    module_disptap.cpp
src/app-messages/
    msg_disptap_data.h / .cpp         (ID 0x0800)
    msg_nozzle_state.h / .cpp         (ID 0x0801)
    msg_fuel_pumped.h / .cpp          (ID 0x0802)
    msg_disptap_fw_version.h / .cpp   (ID 0x0803)
```

### Simulator stub for display-tap hardware
`src/product/ferp-com-simulator/sim_disptap/sim_disptap_injector.cpp`

A soft-timer fires every ~3 s and publishes a scripted sequence of
`MsgDispTapData` frames simulating a complete pump transaction:
1. Nozzle 0 lifted (START)
2. 10 × display-tap frames with increasing volume/price
3. Nozzle 0 replaced (STOP)

Messages can also be injected manually via the Python UI message inject widget.

### Python UI widgets
```
┌──────────────────────────────────┐
│  Nozzle 0          Nozzle 1      │
│  ●  PUMPING        ○  IDLE       │
│  Vol:  12.345 L                  │
│  Unit: 1.85 /L                   │
│  Total: 22.84                    │
└──────────────────────────────────┘
```

### Test
Scripted injection → Python shows nozzle state transitions and final
`MsgFuelPumped` values matching the injected data.

---

## Sprint 5 — Buttons (Print + Default)  🔶 PARTIAL

### What is done ✅
- `src/app-modules/module_default_btn/module_default_btn.h/.cpp` — full implementation, GPIO 36
- `src/app-modules/module_print_btn/module_print_btn.h/.cpp` — full implementation, GPIO 34/35
- `src/app-messages/msg_default_btn.h/.cpp` (ID `0x0900`)
- `src/app-messages/msg_printer_btn.h/.cpp` (ID `0x0901`)
- Both modules compile and run in the simulator (GPIO init logs expected PAL errors on macOS)
- Button events can be injected from the Python UI via the message inject widget

### What remains ⬜
- Dedicated Python UI button widget (clickable `[Short]` / `[Long]` per channel)
  — currently inject-only via the generic message inject widget

---

## Sprint 6 — LEDs + Buzzer  🔶 PARTIAL

### What is done ✅
- `src/app-modules/module_leds/module_leds.h/.cpp` — full implementation
- Uses `hsys_led` peripheral + `hsys_soft_timer`; 250 ms / 2 Hz blink after SPIFFS ready
- LED1 (GPIO 5) and LED2 (GPIO 4) both blink in sync
- Python UI: LED1 and LED2 circles in the System panel track GPIO state via `SIM_GPIO_OUT`

### What remains ⬜
- `module_buzzer` — not yet started
- Additional LED patterns for WiFi, Cloud, and Pump states (Sprint 12 polish)

---

## Sprint 7 — MQTT module  ⬜ PENDING

### Files to create
```
src/app-modules/module_mqtt/
src/app-messages/
    msg_mqtt_event.h / .cpp           (ID 0x0700)
    msg_mqtt_rx_message.h / .cpp      (ID 0x0701)
    msg_mqtt_publish_request.h / .cpp (ID 0x0702)
```

### Simulator stub
`src/product/ferp-com-simulator/sim_mqtt/sim_mqtt_broker_stub.cpp`

- On `MsgInternetStatus(connected)`: publish `MsgMqttEvent(CONNECTED)`
- Python UI has a text field: type a topic + payload, click "Inject" →
  simulator receives `MsgMqttRxMessage` via `SIM_MSG_INJECT`
- Outbound `MsgMqttPublishRequest` → Python UI shows in a "Published" log panel

---

## Sprint 8 — Cloud module  ⬜ PENDING

### Files to create
```
src/app-modules/module_cloud/
src/app-messages/
    msg_cloud_status.h / .cpp   (ID 0x0600)
```

### Simulator stub
`src/product/ferp-com-simulator/sim_cloud/sim_cloud_driver_stub.cpp`

Implements `cloud_driver_t` function pointers with stubs that log their
arguments and return `ERROR_OK`. Python UI shows:
- "Register" / "Startup" / "Heartbeat" / "Pumped" call log with timestamps

---

## Sprint 9 — OTA module  ⬜ PENDING

### Files to create
```
src/app-modules/module_ota/
src/app-messages/
    msg_ota_event.h / .cpp     (ID 0x0B00)
    msg_ota_trigger.h / .cpp   (ID 0x0B01)
```

### Simulator stubs
- `sim_ota_server_stub.cpp`: implements `fp_check_version` returning a fake
  "new version available" response after `MsgInternetStatus` is connected.
  `fp_download_and_flash` sleeps for 2 s (simulating download), returns OK
  but does **not** call `pal_power_reset()` in the simulator.

### Python UI
- OTA status card: "IDLE / CHECKING / DOWNLOADING (n%) / COMPLETE"
- Progress bar driven by `MsgOtaEvent(DOWNLOAD_PROGRESS)`
- "Trigger OTA" button → sends `SIM_MSG_INJECT` with `MsgOtaTrigger`

---

## Sprint 10 — Time, Storage, Retransmit  ⬜ PENDING

Lower urgency — can run later.

```
src/app-modules/module_time/
src/app-modules/module_sd/
src/app-modules/module_retransmit/
```

Simulator stubs: in-memory file maps for SD.

---

## Sprint 11 — WebServer module  ⬜ PENDING

Lowest priority — only needed once WiFi AP mode works on hardware.

---

## Sprint 12 — Cleanup  �� PARTIAL

### Done ✅
- `module_a` and `module_b` disabled in `k_module_table` + `k_task_table` in `app.cpp`
- `hsys_config_mac.cpp` deleted (ArduinoJson used everywhere)
- `pal_mac_system.h` deleted
- `mac_driver` + `module_sim_bridge` moved to `pal/mac-pc/driver/`
- JSON message descriptors moved from `tools/sim-ui/` to `src/` alongside C++ sources

### Remaining ⬜
- Remove `module_a` / `module_b` source files entirely once confirmed unneeded
- Remove old `event_table_t` glue from `app_common.h` if still present
- Final memory pool sizing from sysmon peak-usage reports (hardware run)
- Resolve duplicate-library linker warnings in `ferp-com-simulator` CMakeLists

---

## File / folder conventions

```
src/
  app-messages/
    msg_<name>.h              ← typed message class (+ from_json() under FERP_SIMULATOR)
    msg_<name>.cpp            ← serialise / deserialise / from_json() implementation
    messages/                 ← JSON descriptor files for Python UI
      <Category>/
        msg_<name>.json

  app-modules/
    module_<name>/
      module_<name>.h         ← HsysModule subclass
      module_<name>.cpp
    modules.json              ← module ID registry consumed by Python UI

  product/
    app/
      app.cpp                 ← module table, pool table, task table (shared)
      app_msg_ids.h           ← single ID registry (add IDs here each sprint)
      app_msg_table.h         ← descriptor table
      user_config.h           ← force-included first in every TU

    ferp-com-simulator/
      main/main.cpp           ← app_platform_pre_init + app_run + main()
      CMakeLists.txt
      SPIFFS/spiffs/          ← POSIX SPIFFS emulation root

    ferp-com-esp32-idf/
      main/main.cpp           ← app_init() + while(true){app_run()} only

  sub-modules/
    pal/
      mac-pc/
        pal_mac_*.cpp               ← PAL interface implementations (macOS)
        driver/
          mac_driver.h/.cpp         ← TCP engine (macOS only)
          module_sim_bridge.h/.cpp  ← JSON bridge module (macOS only)
          sim_msg_inject.h/.cpp     ← SIM_MSG_INJECT handler (macOS only)

tools/
  sim-ui/
    sim_ui.py
    widgets/
      led_widget.py
      nozzle_widget.py
      log_widget.py
      config_widget.py
      message_inject_widget.py
    README.md
```

---

## Python UI — communication protocol

### TCP duplex (current implementation, port 9000)

- **Simulator → Python**: `{"dir":"out","ts":1234,"id":"MSG_WIFI_EVENT","data":{...}}\n`
- **Python → Simulator**: `{"dir":"in","id":"SIM_MSG_INJECT","msg_id":256,"src_module_id":3,"dst_module_id":7,"payload":{...}}\n`
- `ModuleSimBridge` owns the TCP server socket; `sim_msg_inject_handle()` dispatches inbound

### JSON event catalogue (grows with each sprint)
```json
{ "id": "MSG_TICK_1000MS",     "data": { "count": 42 } }
{ "id": "MSG_SPIFFS_READY",    "data": {} }
{ "id": "MSG_CONFIG_READY",    "data": { "wifi_ssid": "...", "cloud_url": "..." } }
{ "id": "MSG_WIFI_EVENT",      "data": { "event": "GOT_IP", "ip": "192.168.1.100", "rssi": -65 } }
{ "id": "MSG_INTERNET_STATUS", "data": { "connected": true } }
{ "id": "MSG_NOZZLE_STATE",    "data": { "idx": 0, "state": "PUMPING", "ts": 12345 } }
{ "id": "MSG_FUEL_PUMPED",     "data": { "idx": 0, "vol_l": 12.345, "unit_p": 1.85, "total_p": 22.84 } }
{ "id": "MSG_CLOUD_STATUS",    "data": { "event": "PUMPED_SUCCESS" } }
{ "id": "MSG_OTA_EVENT",       "data": { "event": "DOWNLOADING", "pct": 37 } }
{ "id": "SIM_GPIO_OUT",        "data": { "pin": 5, "level": 1 } }
```

---

## What can be tested in the simulator vs hardware-only

| Feature | Simulator testable? | Status |
|---|---|---|
| Module lifecycle (init/post_init) | ✅ fully | ✅ Done |
| Message routing & subscriptions | ✅ fully | ✅ Done |
| Shared timer service | ✅ fully | ✅ Done |
| Config load / update | ✅ with SPIFFS stub | ✅ Done |
| SPIFFS mount + read/write | ✅ POSIX emulation | ✅ Done |
| LED patterns | ✅ via pal_gpio + SIM_GPIO_OUT | ✅ Done — LED1/LED2 in Python UI |
| Message injection from UI | ✅ SIM_MSG_INJECT + from_json() | ✅ Done |
| Button events (inject) | ✅ via message inject widget | ✅ Done (dedicated widget ⬜ Sprint 5) |
| WiFi state machine | ✅ with stub | ⬜ Sprint 2 |
| Internet detection | ✅ with stub | ⬜ Sprint 3 |
| Fuel state machine (Sanki) | ✅ with injector | ⬜ Sprint 4 |
| Buzzer patterns | ✅ via weak PAL stub | ⬜ Sprint 6 |
| MQTT broker | ✅ with stub | ⬜ Sprint 7 |
| Cloud HTTP | ✅ with stub | ⬜ Sprint 8 |
| OTA cloud-pull | ✅ with stub | ⬜ Sprint 9 |
| OTA web-upload | ✅ with stub | ⬜ Sprint 9 |
| SPIFFS / SD read-write | ✅ in-memory map | ⬜ Sprint 10 |
| Retransmit retry | ✅ with stub | ⬜ Sprint 10 |
| Real WiFi RF | ❌ hardware only | |
| Real UART display-tap | ❌ hardware only | |
| Real flash write (OTA) | ❌ hardware only | |
| Real SD card | ❌ hardware only | |

---

## Quick-start commands

```bash
# Configure + build simulator
cd src/product/ferp-com-simulator
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run with Python UI
./build/ferp-com-simulator &
python3 ../../../../tools/sim-ui/sim_ui.py --port 9000

# Build ESP-IDF (verify no regressions each sprint)
cd ../ferp-com-esp32-idf
idf.py build
```
