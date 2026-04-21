# `ModuleWifi` — Implementation Plan

> **Status:** Planning  
> **Based on:** `old-app/application/app_wifi/`  
> **Module ID:** 15  
> **Target branch:** `main`

---

## 1. What the old code did — and what changes

| Concern | Old `app_wifi` | New `ModuleWifi` |
|---|---|---|
| Config source | `fp_app_wifi_get_init_config()` callback — caller responsible | Subscribe to `MsgConfigReady`, read `app_config_get()` directly |
| PAL WiFi events | `pal_wifi_event_callback_t` — raw PAL callback sets event-group flag + calls `_wake()` | PAL callback posts `MsgWifiEvent` onto the bus via a static helper |
| Periodic RSSI check | Raw `hsys_timer` inside module + 30 s poll in `wifi_app_monitoring` | `MsgTimerStart` (repetitive, 30 s) → `MsgTimerAlarm` |
| Connection timeout / retry | `board_millis()` diff in `wifi_app_connecting` state | One-shot `MsgTimerStart` (60 s) for connect timeout |
| Retry limit | `USER_MAX_NO_WIFI_CONNECT_RETRY_COUNT` hardcoded constant | Same constant, published via `MsgWifiEvent` with `WIFI_EVENT_NO_SIGNAL` |
| Publish result | `fp_app_wifi_on_event_t` callback — point-to-point | `publish(MsgWifiEvent)` — bus fan-out |
| PAL init | Done inside `app_wifi_run()` state machine | Done inside `ModuleWifi::init()` after `MsgConfigReady` (or lazy on first run) |
| Simulator | No wifi on mac — previously no-op | `pal_mac_wifi.cpp` stub satisfies linker; real events injected from UI via `SIM_MSG_INJECT` |

---

## 2. State machine

```
             ┌───────────────────────────┐
             │  WAIT_FOR_CONFIG          │ ◄── startup
             └──────────┬────────────────┘
                        │ MsgConfigReady
                        ▼
             ┌───────────────────────────┐
             │  INITIALISING             │ pal_wifi_init() + pal_wifi_start()
             └──────────┬────────────────┘
                        │ PAL callback → GOT_IP  (or instant in sim)
                        ▼
             ┌───────────────────────────┐       ┌─────────────────────────┐
             │  CONNECTING               │──────►│  RECONNECTING           │
             │  (connect timeout: 60 s)  │◄──────│  (retry, inc counter)   │
             └──────────┬────────────────┘       └─────────────────────────┘
                        │ PAL_WIFI_EVENT_STA_GOT_IP
                        ▼
             ┌───────────────────────────┐
             │  MONITORING               │
             │  RSSI poll every 30 s     │──── MsgTimerAlarm (repetitive)
             └──────────┬────────────────┘
        DISCONNECTED    │
        ────────────────┘ → back to RECONNECTING
```

**State transitions:**

| From | Trigger | To | Side-effect |
|---|---|---|---|
| `WAIT_FOR_CONFIG` | `MsgConfigReady` | `INITIALISING` | `pal_wifi_init()` + `pal_wifi_start()` + `pal_wifi_sta_connect()` |
| `INITIALISING` | PAL → `STA_GOT_IP` | `MONITORING` | arm RSSI timer; publish `MsgWifiEvent(GOT_IP)` |
| `INITIALISING` / `CONNECTING` | connect timeout (60 s) | `RECONNECTING` | retry + increment counter |
| `RECONNECTING` | always | `CONNECTING` | `pal_wifi_sta_disconnect()` + `pal_wifi_sta_connect()` |
| `MONITORING` | PAL → `STA_DISCONNECTED` | `RECONNECTING` | stop RSSI timer; publish `MsgWifiEvent(DISCONNECTED)` |
| `MONITORING` | RSSI timer alarm | `MONITORING` | `pal_wifi_sta_get_rssi()` → publish `MsgWifiEvent(RSSI_CHANGED)` |
| `CONNECTING` | retry limit exceeded | `CONNECTING` | publish `MsgWifiEvent(NO_SIGNAL)` + reset counter |

---

## 3. PAL callback → HSYS bus bridge

The PAL WiFi callback fires from a **PAL thread** (not the `ModuleWifi` task thread).  
It must be kept minimal — just build + post the message:

```
PAL thread                               ModuleWifi task
─────────────                            ──────────────────
pal_wifi_event_callback()
  │
  └─► s_pending_event = event            ← atomic write
      module->notify()  ◄── wakes task ─────────────────► on_msg_received()
                                              reads s_pending_event
                                              publishes MsgWifiEvent
```

In the simulator there is **no real PAL WiFi driver** — events come from the
Python UI via `SIM_MSG_INJECT → MsgWifiEvent`.  `ModuleWifi` simply subscribes
to `MsgWifiEvent` like every other module and re-publishes it after enriching
with SSID / IP / MAC from its own cached config.  The `pal_mac_wifi.cpp` stub
satisfies the linker but all functions return 0 / do nothing.

---

## 4. Files to create / modify

```
src/
├── app-modules/
│   └── module_wifi/              ← NEW
│       ├── module_wifi.h
│       └── module_wifi.cpp
│
├── app-messages/
│   └── (msg_wifi_event already exists ✅)
│
├── product/
│   ├── app/
│   │   ├── app.cpp               ← add #include, module table, task table
│   │   └── app_msg_ids.h         ← already has MSG_ID_WIFI_EVENT ✅
│   └── ferp-com-simulator/
│       └── CMakeLists.txt        ← add module_wifi.cpp + pal_mac_wifi.cpp
│
├── sub-modules/
│   └── pal/
│       └── mac-pc/
│           └── pal_mac_wifi.cpp  ← NEW (stub — all fns return 0 / no-op)
│
└── app-messages/messages/Network/
    └── (msg_wifi_event.json already exists ✅)
```

---

## 5. `pal_mac_wifi.cpp` — simulator stub strategy

The simulator does **not** manage a real WiFi connection.  WiFi events are
injected from the Python UI via `SIM_MSG_INJECT → MsgWifiEvent`.  The stub
only needs to satisfy the linker so `ModuleWifi` can call `pal_wifi_init()` etc.
without `#ifdef` guards in the module.

```
pal_wifi_init()          → return 0 (success, no-op)
pal_wifi_start()         → return 0
pal_wifi_sta_connect()   → return 0
pal_wifi_sta_disconnect()→ return 0
pal_wifi_sta_is_connected() → return _sim_connected  (toggled by injected events)
pal_wifi_sta_get_rssi()  → return _sim_rssi           (set by injected events)
pal_wifi_get_mac_str()   → return "AA:BB:CC:DD:EE:FF"
pal_wifi_get_ip_str()    → return "192.168.1.100"
pal_wifi_get_status()    → fill from sim state
```

The stub maintains two file-scope variables `_sim_connected` and `_sim_rssi`
that `ModuleWifi` can query during its `MONITORING` state exactly as it would
on the device.

---

## 6. `ModuleWifi` — key constants

| Constant | Value | Source |
|---|---|---|
| `MODULE_WIFI_ID` | `15` | next after ModuleInternet (14) |
| `MODULE_WIFI_CONNECT_TIMEOUT_MS` | `60 000` | matches old app |
| `MODULE_WIFI_RSSI_INTERVAL_MS` | `30 000` | matches old app |
| `MODULE_WIFI_MAX_RETRY` | `USER_MAX_NO_WIFI_CONNECT_RETRY_COUNT` | from `app.h` / user_config |

---

## 7. Messages published

| Message | When | Payload fields set |
|---|---|---|
| `MsgWifiEvent(STA_CONNECTED)` | PAL → `STA_CONNECTED` | ssid, mac |
| `MsgWifiEvent(STA_GOT_IP)` | PAL → `STA_GOT_IP` | ssid, ip, mac, rssi |
| `MsgWifiEvent(STA_DISCONNECTED)` | PAL → `STA_DISCONNECTED` | — |
| `MsgWifiEvent(STA_RSSI_CHANGED)` | RSSI timer alarm | rssi |
| `MsgWifiEvent(AP_START)` | PAL → `AP_START` | — |
| `MsgWifiEvent(AP_STOP)` | PAL → `AP_STOP` | — |

---

## 8. Simulator flow end-to-end (with ModuleWifi)

```
Python UI                    Simulator binary
─────────────────────────────────────────────────────────────
[Messages tab]
Select: msg_wifi_event
Set: event=GOT_IP
     ip=192.168.1.100
     ssid=MyNetwork
     mac=AA:BB:CC:DD:EE:FF
     rssi=-55
Click: Inject
    │
    │ {"id":"SIM_MSG_INJECT",
    │  "data":{"msg_id":0x0A00, ...}}
    │
    ▼ (TCP)
sim_msg_inject.cpp
    │ MsgWifiEvent::from_json()
    │ hsys_msg_publish()
    ▼
HSYS bus ──────────────────────────────────────────────────┐
                                                           │
              ModuleWifi::on_msg_received()                │
              (in sim: pass-through + enrich)              │
                    │ publish MsgWifiEvent(GOT_IP)         │
                    ▼                                      │
              ModuleInternet: WAIT_FOR_WIFI → CHECKING     │
              ModuleCloud:    WAIT_FOR_INTERNET            │
              ModuleSimBridge: forward to UI               │
                    │                                      │
    ◄───────────────┘ TCP                                  │
    {"id":"MSG_WIFI_EVENT",                                │
     "data":{"event":"GOT_IP","rssi":-55,...}}             │
    ▼                                                      │
  _led_wifi.set_on(True)                                   │
  rssi bar updated                                         │
```

---

## 9. Implementation sequence

```
Step 1 ── pal_mac_wifi.cpp
           All stubs; _sim_connected / _sim_rssi state

Step 2 ── module_wifi.h / module_wifi.cpp
           State machine + PAL bridge + re-publish

Step 3 ── app.cpp
           Add to module table + task table

Step 4 ── CMakeLists.txt (simulator)
           Add pal_mac_wifi.cpp + module_wifi.cpp

Step 5 ── modules.json
           id=15 ModuleWifi

Step 6 ── Build + smoke test
           Inject MsgWifiEvent(GOT_IP) → watch Internet + Cloud chain react
```

---

## 10. What is NOT in scope

- AP mode full implementation (stub is sufficient for current product)
- WPA3 / enterprise auth
- WiFi scan / SSID list
- `pal_esp32_wifi.cpp` — already exists in `src/sub-modules/pal/esp-idf/`
- NTP sync trigger on GOT_IP — separate future module
