# Porting Plan — Old Event-Based App → HSYS Message Queue Architecture

## Philosophy

- **One module per sprint.** Each sprint ends with the simulator running and the
  Python UI showing the correct visual state for that module.
- **Stub-first.** Every new module is created as a stub (compiles, publishes
  nothing, subscribes to nothing) before real logic is added. This keeps the
  build green at every commit.
- **Test at the task boundary.** The simulator + Python UI is the test bench.
  Real hardware is only needed for things the simulator physically cannot do
  (UART tap, SD card, actual WiFi RF).
- **No regressions.** The old demo modules (module_a, module_b, ticker,
  sysmon) stay in the build until explicitly replaced.

---

## Layer diagram (what you are building toward)

```
┌─────────────────────────────────────────────────────────────────┐
│  Python UI  (TCP socket, localhost:9000)                        │
│  • LED circles  • nozzle state  • WiFi/internet bars            │
│  • OTA progress  • MQTT log  • cloud heartbeat counter          │
└───────────────────────┬─────────────────────────────────────────┘
                        │  JSON lines  (stdin of Python / TCP)
┌───────────────────────▼─────────────────────────────────────────┐
│  ferp-com-simulator  (macOS native C++17 binary)                │
│                                                                 │
│  ModuleSimBridge  ← NEW helper module                           │
│    subscribes to every "UI-visible" message and serialises      │
│    it as a JSON line to stdout / TCP socket                     │
│                                                                 │
│  [All real app modules running in their tasks]                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Step 0 — Infrastructure: SimBridge + Python UI skeleton

> Do this BEFORE any domain module. It gives you a visualisation loop you can
> verify immediately with existing demo messages.

### 0-A  `ModuleSimBridge` (C++)

A single new module that lives **only** in the simulator product.

- Subscribes to every message that has a visible UI effect
- On `on_msg_received()`: serialises the message as a one-line JSON string and
  writes it to `stdout` (or a TCP socket on port 9000)
- Format: `{"ts":<ms>,"id":"MSG_NAME","data":{...}}\n`
- Uses the same `log()` plumbing but on a separate `stdout` stream so log
  output and UI events don't mix

File to create: `src/product/ferp-com-simulator/sim_bridge/module_sim_bridge.cpp/.h`

### 0-B  Python UI (`tools/sim-ui/`)

Start with a minimal [Tkinter](https://docs.python.org/3/library/tkinter.html)
app — no extra dependencies, ships with every Python installation.

```
tools/
  sim-ui/
    sim_ui.py          ← main window, reads stdin or TCP
    widgets/
      led_widget.py    ← coloured circle that can blink
      nozzle_widget.py ← nozzle state card
      log_widget.py    ← scrolling text for raw log lines
    README.md
```

Launch sequence (two terminals):

```bash
# Terminal 1 — simulator, pipe UI events to python
./build/ferp-com-simulator --ui-port 9000

# Terminal 2 — Python UI
python3 tools/sim-ui/sim_ui.py --port 9000
```

Or for the simple stdin pipe approach:

```bash
./build/ferp-com-simulator 2>sim.log | python3 tools/sim-ui/sim_ui.py
```

### 0-C  Verify with existing messages

With the demo modules still in place you should see:
- `MsgTick1000ms` → Python UI shows a heartbeat counter incrementing every second
- `MsgSensorData` → a numeric display updating

**✅ Gate: Python UI is alive and showing tick + sensor data before moving on.**

---

## Sprint 1 — Config module

### Why first?
Every other module needs configuration (WiFi SSID, cloud URL, MQTT broker).
Getting `ModuleConfig` right early means every later module can subscribe to
`MsgConfigLoaded` and get real values.

### Files to create
```
src/app-modules/module_config/
    module_config.h
    module_config.cpp
src/app-messages/
    msg_config_loaded.h / .cpp        (ID 0x0201)
    msg_config_update_request.h / .cpp (ID 0x0202)
    msg_config_reset_request.h / .cpp  (ID 0x0203)
```

### Simulator stub for config
Create `src/product/ferp-com-simulator/sim_config/sim_config_provider.cpp`  
Hardcodes a test `app_config_t` (fake SSID, fake cloud URL, etc.) and
publishes `MsgConfigLoaded` once at startup.  On real hardware this reads
SPIFFS/NVS.

### SimBridge subscription
Add `MsgConfigLoaded` → Python shows "Config loaded" badge.

### Test in simulator
- Simulator starts → Python shows "Config: loaded"
- Publish `MsgConfigUpdateRequest` manually from a debug command → Python shows badge update

---

## Sprint 2 — WiFi module

### Files to create
```
src/app-modules/module_wifi/
    module_wifi.h
    module_wifi.cpp
src/app-messages/
    msg_wifi_event.h / .cpp   (ID 0x0301)
```

### Simulator stub for WiFi
`src/product/ferp-com-simulator/sim_wifi/sim_wifi_module.cpp`  
Subscribes to `MsgConfigLoaded`, then after a configurable delay (e.g. 2 s)
publishes a fake `MsgWifiEvent(STA_GOT_IP, ip=192.168.1.100)`.

### Python UI widget
- WiFi signal bar widget (0–4 bars)
- Shows "STA_CONNECTED", "GOT_IP 192.168.1.100", "DISCONNECTED"

### Test
1. Simulator starts → Config loads → WiFi "connects" after 2 s → Python shows bars
2. Manually trigger `STA_DISCONNECTED` → bars drop to 0

---

## Sprint 3 — Internet module

### Files to create
```
src/app-modules/module_internet/
    module_internet.h
    module_internet.cpp
src/app-messages/
    msg_internet_status.h / .cpp   (ID 0x0401)
```

### Simulator stub
Subscribes to `MsgWifiEvent(GOT_IP)`, publishes `MsgInternetStatus(connected=true)`
after a 1 s delay.  No actual HTTP ping needed in simulator.

### Python UI widget
Globe icon: grey = disconnected, green = connected.

---

## Sprint 4 — Fuel / DispTap modules  *(the core domain)*

> This is the most important module to test in the simulator because it has
> the most complex state machine and it is impossible to test without hardware
> **unless** you write a simulator stub that injects fake display-tap frames.

### Files to create
```
src/app-modules/module_fuel/
    module_fuel.h
    module_fuel.cpp
src/app-modules/module_disptap/
    module_disptap.h
    module_disptap.cpp
src/app-messages/
    msg_disptap_data.h / .cpp         (ID 0x0701)
    msg_nozzle_state.h / .cpp         (ID 0x0702)
    msg_fuel_pumped.h / .cpp          (ID 0x0703)
    msg_disptap_fw_version.h / .cpp   (ID 0x0704)
```

### Simulator stub for display-tap hardware
`src/product/ferp-com-simulator/sim_disptap/sim_disptap_injector.cpp`

A soft-timer fires every ~3 s and publishes a scripted sequence of
`MsgDispTapData` frames that simulate a complete pump transaction:
1. Nozzle 0 lifted (START)
2. 10 × display-tap frames with increasing volume/price
3. Nozzle 0 replaced (STOP)

The Sanki state machine inside `ModuleFuel` processes these frames exactly as
it would process real serial frames on hardware.

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
- Circle: grey=idle, yellow=pumping, green=pumped(flash)
- Live values update on each `MsgDispTapData` (or only on `MsgFuelPumped` if you want less traffic)

### Test
Scripted injection → Python shows nozzle state transitions and final
`MsgFuelPumped` values matching the injected data.

---

## Sprint 5 — Buttons (Print + Default)

### Files to create
```
src/app-modules/module_print_btn/
    module_print_btn.h
    module_print_btn.cpp
src/app-modules/module_default_btn/
    module_default_btn.h
    module_default_btn.cpp
src/app-messages/
    msg_print_btn_event.h / .cpp     (ID 0x0801)
    msg_default_btn_event.h / .cpp   (ID 0x0901)
```

### Simulator stub
`src/product/ferp-com-simulator/sim_buttons/sim_button_injector.cpp`

Reads keyboard input (non-blocking stdin read or a TCP command from Python UI)
and publishes the corresponding `MsgPrintBtnEvent` or `MsgDefaultBtnEvent`.

### Python UI widgets
Four clickable buttons in the UI:
- `[Print 1 Short]` `[Print 1 Long]`
- `[Print 2 Short]` `[Print 2 Long]`
- `[Default Short]` `[Default Long]`

Clicking a button sends a JSON command back to the simulator over the TCP
socket which the `sim_button_injector` converts to a published message.

---

## Sprint 6 — LEDs + Buzzer

### Files to create
```
src/app-modules/module_leds/
src/app-modules/module_buzzer/
```
No new messages needed — these only subscribe.

### Python UI
- 3–4 coloured circles for LEDs (Power, WiFi, Cloud, Pump)
- Each circle blinks with a configurable pattern driven by `ModuleLeds`
- `SimBridge` intercepts LED PAL calls via a weak stub `pal_led_set()` and
  publishes a `SimLedState` JSON event

### Buzzer
- Python shows a "🔔 BEEP" toast notification for 300 ms when a buzzer
  PAL call fires

---

## Sprint 7 — MQTT module

### Files to create
```
src/app-modules/module_mqtt/
src/app-messages/
    msg_mqtt_event.h / .cpp           (ID 0x0601)
    msg_mqtt_rx_message.h / .cpp      (ID 0x0602)
    msg_mqtt_publish_request.h / .cpp (ID 0x0603)
```

### Simulator stub
`src/product/ferp-com-simulator/sim_mqtt/sim_mqtt_broker_stub.cpp`

- On `MsgInternetStatus(connected)`: publish `MsgMqttEvent(CONNECTED)`
- Python UI has a text field: type a topic + payload, click "Inject" →
  simulator receives `MsgMqttRxMessage`
- Outbound `MsgMqttPublishRequest` → Python UI shows in a "Published" log panel

---

## Sprint 8 — Cloud module

### Files to create
```
src/app-modules/module_cloud/
src/app-messages/
    msg_cloud_status.h / .cpp   (ID 0x0501)
```

### Simulator stub
`src/product/ferp-com-simulator/sim_cloud/sim_cloud_driver_stub.cpp`

Implements `cloud_driver_t` function pointers with stubs that log their
arguments and return `ERROR_OK`.  Python UI shows:
- "Register" / "Startup" / "Heartbeat" / "Pumped" call log with timestamps

---

## Sprint 9 — OTA module

### Files to create
```
src/app-modules/module_ota/
src/app-messages/
    msg_ota_event.h / .cpp     (ID 0x0A01)
    msg_ota_trigger.h / .cpp   (ID 0x0A02)
```

### Simulator stubs
- `sim_ota_server_stub.cpp`: implements `fp_check_version` returning a fake
  "new version available" response after `MsgInternetStatus` is connected.
  `fp_download_and_flash` sleeps for 2 s (simulating download), returns OK
  but does **not** call `pal_power_reset()` in the simulator.
- Web-upload path: a small Python HTTP client in the UI can POST a fake binary
  to the simulator's embedded HTTP server stub.

### Python UI
- OTA status card: "IDLE / CHECKING / DOWNLOADING (n%) / COMPLETE"
- Progress bar driven by `MsgOtaEvent(DOWNLOAD_PROGRESS)`
- "Trigger OTA" button → sends MQTT-style JSON command to simulator

---

## Sprint 10 — Time, Storage, Retransmit

Lower urgency — can run later.

```
src/app-modules/module_time/
src/app-modules/module_spiffs/   (or module_storage/ shared)
src/app-modules/module_sd/
src/app-modules/module_retransmit/
```

Simulator stubs: in-memory file maps for SPIFFS/SD.

---

## Sprint 11 — WebServer module

Lowest priority — only needed once WiFi AP mode works on hardware.

---

## Sprint 12 — Cleanup

- Remove demo modules (module_a, module_b, ticker demo)
- Remove old `event_table_t` glue from app_common.h
- Final memory pool sizing based on peak usage from sysmon reports

---

## File / folder conventions

```
src/
  app-messages/
    msg_<name>.h              ← typed message class declaration
    msg_<name>.cpp            ← constructor / static ID

  app-modules/
    module_<name>/
      module_<name>.h         ← HsysModule subclass
      module_<name>.cpp

  product/
    app/
      app.cpp                 ← module table, pool table, task table (shared)
      app_msg_ids.h           ← single ID registry (add IDs here each sprint)
      app_msg_table.h         ← descriptor table

    ferp-com-simulator/
      sim_<name>/             ← one stub folder per sprint
        sim_<name>_module.cpp
      sim_bridge/
        module_sim_bridge.cpp ← JSON serialiser for Python UI
      CMakeLists.txt

    ferp-com-esp32-idf/
      main/
        hello_world_main.c    ← unchanged until all modules compile on IDF

tools/
  sim-ui/
    sim_ui.py
    widgets/
      led_widget.py
      nozzle_widget.py
      mqtt_widget.py
      ota_widget.py
      log_widget.py
    README.md
```

---

## Python UI — communication protocol

Two modes, selected at launch:

### Mode A — stdin pipe (simplest, no sockets)
```bash
./build/ferp-com-simulator | python3 tools/sim-ui/sim_ui.py
```
- Simulator writes JSON UI events to stdout
- Simulator log goes to stderr (separate streams)
- Python reads stdin line by line, parses JSON, updates widgets
- Limitation: no bidirectional communication (buttons can't inject messages)

### Mode B — TCP duplex (recommended)
```bash
./build/ferp-com-simulator --ui-port 9000 &
python3 tools/sim-ui/sim_ui.py --port 9000
```
- Simulator opens TCP server on 9000 at startup
- Python connects as client
- **Simulator → Python**: `{"dir":"out","ts":1234,"id":"MSG_WIFI_EVENT","data":{"event":"GOT_IP","ip":"192.168.1.100"}}\n`
- **Python → Simulator**: `{"dir":"in","id":"SIM_BTN","data":{"btn":"print1_short"}}\n`
- `ModuleSimBridge` owns the TCP server socket inside the simulator

### JSON event catalogue (grows with each sprint)
```json
{"id":"MSG_TICK_1000MS",       "data":{"count":42}}
{"id":"MSG_WIFI_EVENT",        "data":{"event":"GOT_IP","ip":"192.168.1.100","rssi":-65}}
{"id":"MSG_INTERNET_STATUS",   "data":{"connected":true}}
{"id":"MSG_NOZZLE_STATE",      "data":{"idx":0,"state":"PUMPING","ts":12345}}
{"id":"MSG_FUEL_PUMPED",       "data":{"idx":0,"vol_l":12.345,"unit_p":1.85,"total_p":22.84}}
{"id":"MSG_CLOUD_STATUS",      "data":{"event":"PUMPED_SUCCESS"}}
{"id":"MSG_OTA_EVENT",         "data":{"event":"DOWNLOADING","driver":0,"pct":37}}
{"id":"SIM_LED",               "data":{"led":"wifi","state":"on"}}
{"id":"SIM_BUZZER",            "data":{"pattern":"double_beep"}}
```

---

## What can be tested in the simulator vs hardware-only

| Feature | Simulator testable? | Notes |
|---|---|---|
| Module lifecycle (init/post_init) | ✅ fully | Already working |
| Message routing & subscriptions | ✅ fully | Core framework |
| Config load / update | ✅ with stub | Fake config values |
| WiFi state machine | ✅ with stub | Inject fake events |
| Internet detection | ✅ with stub | Inject connected/disconnected |
| Fuel state machine (Sanki) | ✅ with injector | Scripted frame sequences |
| Button debounce | ✅ with keyboard/TCP | Python UI sends button events |
| LED patterns | ✅ via weak PAL stub | Python shows circles |
| Buzzer patterns | ✅ via weak PAL stub | Python shows toast |
| MQTT broker | ✅ with stub | Python injects RX messages |
| Cloud HTTP | ✅ with stub | Stub driver logs calls |
| OTA cloud-pull | ✅ with stub | Stub returns fake version |
| OTA web-upload | ✅ with stub | Python HTTP client |
| SPIFFS / SD read-write | ✅ in-memory map | No filesystem needed |
| Retransmit retry | ✅ with stub | Inject cloud-fail then cloud-ok |
| Real WiFi RF | ❌ hardware only | |
| Real UART display-tap | ❌ hardware only | |
| Real flash write (OTA) | ❌ hardware only | |
| Real SD card | ❌ hardware only | |

---

## Quick-start commands for each sprint

```bash
# Configure + build simulator
cd src/product/ferp-com-simulator
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run with Python UI (Mode B)
./build/ferp-com-simulator --ui-port 9000 &
python3 ../../../../tools/sim-ui/sim_ui.py --port 9000

# Build ESP-IDF (verify no regressions each sprint)
cd ../ferp-com-esp32-idf
idf.py build
```
