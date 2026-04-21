# `ModuleInternet` — Implementation Plan

> **Status:** Planning  
> **Based on:** `old-app/application/app_internet/`  
> **Target branch:** `main`

---

## 1. What the old code did

| Concern | Old approach | Problem |
|---|---|---|
| WiFi status | Callback injected via `event_table->on_wifi_event` | Tight coupling to `app_wifi` struct |
| Periodic check | Raw `hsys_timer` created inline | Not portable, bypasses `ModuleTimer` |
| Task wake-up | `fp_wake_t` function pointer | Race-prone, manual |
| State | `enum app_state_t` + file-scope statics | Not encapsulated |
| Publish result | Callback `fp_app_internet_on_event_t` | Caller must register; no bus fanout |
| PAL | `pal_network_ping()` directly | Already correct — keep this |

---

## 2. New design — message-driven

```
MsgWifiEvent (GOT_IP / DISCONNECTED)
        │
        ▼
  ┌─────────────────────────────────────────────────────┐
  │              ModuleInternet  (id=14)                │
  │                                                     │
  │  STATE_WAIT_FOR_WIFI                                │
  │    └─[GOT_IP]──────────────────────────────────┐   │
  │  STATE_CHECKING                                 │   │
  │    └─[ping ok]──► publish MsgInternetStatus(true)  │
  │    └─[ping fail]─► publish MsgInternetStatus(false) │
  │    └─[DISCONNECTED]──────────────────────────┐  │   │
  │  STATE_WAIT_FOR_WIFI  ◄──────────────────────┘  │   │
  │                        ◄────────────────────────┘   │
  └─────────────────────────────────────────────────────┘
        │
        ▼
  MsgInternetStatus (connected: bool)
  → ModuleCloud subscribes
  → ModuleSimBridge subscribes (forwards to UI)
```

### State machine

```
              ┌──────────────────────┐
              │   WAIT_FOR_WIFI      │◄──── startup
              └──────────┬───────────┘
                         │ MsgWifiEvent::GOT_IP
                         ▼
              ┌──────────────────────┐
              │      CHECKING        │──── arm one-shot timer
              │  (ping on entry +    │     (interval: 60 s)
              │   on every alarm)    │
              └──┬───────────────────┘
   DISCONNECTED  │      ▲ MsgTimerAlarm
   ──────────────┘      └─────────── (repeat while in CHECKING)
```

**Transitions:**

| From | Event | To | Side-effect |
|---|---|---|---|
| `WAIT_FOR_WIFI` | `MsgWifiEvent::GOT_IP` | `CHECKING` | arm timer; run ping immediately |
| `CHECKING` | `MsgTimerAlarm` | `CHECKING` | run ping; publish `MsgInternetStatus` |
| `CHECKING` | `MsgWifiEvent::STA_DISCONNECTED` | `WAIT_FOR_WIFI` | stop timer; publish `MsgInternetStatus(false)` |

---

## 3. Files to create / modify

```
src/
├── app-modules/
│   ├── module_internet/          ← NEW
│   │   ├── module_internet.h
│   │   └── module_internet.cpp
│   ├── app_msg_ids.h             ← already has MSG_ID_INTERNET_STATUS ✅
│   └── modules.json              ← add id=14 ModuleInternet
│
├── app-messages/
│   └── (msg_internet_status already exists ✅)
│
├── product/
│   ├── app/
│   │   ├── app.cpp               ← add to module + task tables
│   │   └── app_msg_ids.h         ← already updated ✅
│   └── ferp-com-simulator/
│       └── CMakeLists.txt        ← add module_internet.cpp
│
└── sub-modules/
    └── pal/
        ├── mac-pc/
        │   └── pal_mac_network.cpp   ← NEW (macOS ping via TCP connect)
        └── esp32-idf/ (or device pal)
            └── pal_esp32_network.cpp ← NEW (uses ESP-IDF lwIP ping)
```

---

## 4. How the simulator PAL layer works end-to-end

### 4a. Physical device flow

```
ESP32 hardware
─────────────────────────────────────────────────────────────────
  WiFi driver ──► app_wifi (old) / ModuleWifi (new)
                        │ MsgWifiEvent::GOT_IP
                        ▼
                  ModuleInternet
                        │ pal_network_ping("8.8.8.8", 2000)
                        │   (uses lwIP / ESP-IDF ping)
                        │
                        ▼
                  MsgInternetStatus ──► ModuleCloud
```

### 4b. Simulator (macOS) flow

```
Python UI  ◄══════════════════════ TCP socket ══════════════════► Simulator binary
(sim_ui.py)                       port 9000                       (ferp-com-simulator)
                                                                          │
  [Messages tab]                                                          │
  Select: msg_wifi_event                                    ┌─────────────┴──────────────┐
  Set: event=GOT_IP, ip=192.168.1.x                        │     HSYS message bus       │
  Click: Inject                                             └──────────┬─────────────────┘
      │                                                                │
      │ JSON over TCP:                                    MsgWifiEvent │ (from sim_msg_inject)
      │ {"id":"SIM_MSG_INJECT",                                        ▼
      │  "data":{"msg_id":0x0A00,                         ModuleInternet
      │   "payload":{"event":"GOT_IP",                         │
      │    "ip":"192.168.1.100",...}}}                          │ pal_network_ping()
      │                                                          │  (macOS: TCP connect to
      │                                                          │   8.8.8.8:80, or ICMP)
      │                                                          │
      │                                                          ▼
      │                                               MsgInternetStatus(connected=true/false)
      │                                                          │
      │                                               ModuleSimBridge subscribes
      │                                                          │
      │                                                          ▼
      │◄═══ {"id":"MSG_INTERNET_STATUS",              mac_driver_send_json()
             "data":{"connected":true}}
      │
      ▼
  _led_internet.set_on(True)   [Internet LED turns blue]
```

### 4c. Component stack

```
┌─────────────────────────────────────────────────────────────────────┐
│  Application layer                                                  │
│  ModuleCloud ──subscribes──► MsgInternetStatus                      │
└─────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────────┐
│  Module layer                                                       │
│  ModuleInternet                                                     │
│    • subscribes: MsgWifiEvent, MsgTimerAlarm                        │
│    • calls:      pal_network_ping()     ← PAL call                  │
│    • publishes:  MsgInternetStatus                                  │
└─────────────────────────────────────────────────────────────────────┘
┌──────────────────────────┐  ┌──────────────────────────────────────┐
│  PAL — ESP32 (device)    │  │  PAL — mac-pc (simulator)            │
│  pal_esp32_network.cpp   │  │  pal_mac_network.cpp                 │
│  uses ESP-IDF lwIP ping  │  │  uses POSIX socket connect()         │
│  pal_network_ping() ─────┼──┼─ pal_network_ping()                  │
└──────────────────────────┘  └──────────────────────────────────────┘
```

---

## 5. `pal_mac_network.cpp` — macOS strategy

ICMP ping requires root on macOS. Instead use a **TCP connect probe** to `8.8.8.8:53` (Google DNS) with a short timeout — exactly what browsers do.

```
pal_network_ping("8.8.8.8", 2000)
  │
  └─► socket(AF_INET, SOCK_STREAM) + connect timeout 2 s
        ├─ connect() succeeds ──► return true
        └─ ETIMEDOUT / ENETUNREACH ──► return false
```

---

## 6. Module constants

| Constant | Value | Notes |
|---|---|---|
| `MODULE_INTERNET_ID` | `14` | Next free ID after cloud (13) |
| `MODULE_INTERNET_CHECK_INTERVAL_MS` | `60 000` | Matches old app |
| `INTERNET_PING_HOST` | `"8.8.8.8"` | Google DNS, configurable |
| `INTERNET_PING_TIMEOUT_MS` | `2 000` | 2 s per ping |

---

## 7. `MsgTimerStart` parameters

```cpp
MsgTimerStart::Payload{
    .source_module_id = MODULE_INTERNET_ID,
    .start_offset_ms  = 0,
    .duration_ms      = MODULE_INTERNET_CHECK_INTERVAL_MS,
    .is_repetitive    = true,   // repeat every 60 s while wifi up
    .forced           = false,
}
```

> **Note:** Timer is **stopped** (via `MsgTimerStop`) when WiFi disconnects. This avoids ghost ping attempts with no IP.

---

## 8. Implementation sequence

```
Step 1 ── pal_mac_network.cpp
           pal_network_ping() via TCP connect probe

Step 2 ── module_internet.h / module_internet.cpp
           State machine + timer management

Step 3 ── app.cpp
           Add to module table + task table

Step 4 ── CMakeLists.txt (simulator)
           Add module_internet.cpp

Step 5 ── modules.json
           id=14  ModuleInternet

Step 6 ── Build + smoke test
           Inject MsgWifiEvent(GOT_IP) from UI
           Watch Internet LED turn on/off
```

---

## 9. What is NOT in scope

- `pal_esp32_network.cpp` — device PAL can be done in a separate sprint (lwIP ping exists in ESP-IDF)
- DNS resolution check as alternative to ICMP
- Configurable ping host via `MsgConfigReady`
- `ModuleWifi` — still out of scope; WiFi events are injected from UI in simulator
