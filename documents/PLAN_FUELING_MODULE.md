# Fueling Module — Implementation Plan

## Status
- ⬜ **PENDING** — analysis complete, implementation not yet started

---

## 1. Overview

The fueling subsystem reads live transaction data from one or two fuel dispenser
display units ("display taps"), runs it through a protocol-specific state machine
(currently Sanki 6-digit), and emits a single **pumped event** once a complete
transaction is confirmed.  The main ESP32 receives serial frames from a secondary
device (ESP0#### Simulator build guards
All four headers are ESP-IDF specific. In the simulator (Mac) build, `FuelDispTapDriver` must use `#ifndef FERP_SIMULATOR` guards to skip `init_comms_distap()`, `distap_*()`, and `start_serial_flash()`. The simulator provides replacement implementations — see §3-H.

---

### 3-H  Simulator emulation of `com_distap` / `cmd_distap`

Instead of real UART hardware, the simulator provides Mac-side implementations of the
exact same C API, backed by the existing **TCP bridge** to the Python UI.

#### Packet wire format (from `com_distap.c` analysis)

The protocol uses SLIP-like byte-stuffing over UART:

```
On-wire frame:
  0xFF 0xFF          ← SOM (2 bytes, start-of-message marker)
  [escaped payload]  ← pck_id | display | length | ab_data[] | CRC16_LSB | CRC16_MSB
  0xFF               ← EOM

Escaping rules (inside payload only):
  0xFF  →  0xFE 0x00
  0xFE  →  0xFE 0x01

Payload layout (matches data_packet_t packed struct):
  Byte 0:         pck_id     (rx_pckt_id_t / tx_pckt_id_t)
  Byte 1:         display    (display_type_t, 5-bit)
  Byte 2:         length     (number of data bytes in ab_data[])
  Bytes 3..3+N-1: ab_data[]  (N = length)
  Bytes 3+N, 3+N+1: CRC16 (Modbus, seed=0xFFFF, LSB first)
                    CRC covers bytes 0..(3+N-1)

Minimum valid frame: rx_idx >= 4 (pck_id + display + length + 1 data byte)
```

#### Fuel data frame payload (`RX_ID_DIS1_DATA` / `RX_ID_DIS2_DATA`)

When the DT board sends display data, `ab_data` is a raw `display_data_t` struct:

```c
// display_data_t (packed, 13 bytes):
// Byte 0:    data_flags_t flags  { start_stop:1, select_p:1, select_l:1, select_ll:1, rest:4 }
// Byte 1:    data_error_t error  { index:1, unitprice:1, totprice:1, volume:1, price_gap:1, :3 }
// Bytes 2-5: uint32_t total_price   (×0.01, e.g. 1500 = 15.00)
// Bytes 6-9: uint32_t volume_l      (×0.001, e.g. 5000 = 5.000 L)
// Bytes 10-13: uint32_t unit_price  (×0.01, e.g. 300 = 3.00)
// pck_id = RX_ID_DIS1_DATA (0) or RX_ID_DIS2_DATA (1)
// display field = display_type_t enum value
```

#### Command/response exchange (`cmd_distap`)

All `cmd_distap` functions use a **request/response** pattern via `distap_send_cmd()`:

| Host sends (`TX_ID_*`) | DT board replies (`RX_ID_*`) | Key fields |
|---|---|---|
| `TX_ID_DEV_INFO` + `command=0` | `RX_ID_DEV_INFO` + `rx_id_dev_version_t` | `version[32]` string |
| `TX_ID_SET_DISPLAY` + `display` | `RX_ID_SET_DISPLAY` | `state` (0x00=already, 0xFF=changed) |
| `TX_ID_SET_ERR_MASK` + `err_mask` | `RX_ID_SET_ERR_MASK` | `state` (0x00=already, 0xFF=changed) |
| `TX_ID_INPUTS` + `command` | `RX_ID_INPUTS` | `inputs` uint32 (input_pin_t) |

#### Mac simulator implementations

**`mac_com_distap.cpp`** (replaces `com_distap.c` in `FERP_SIMULATOR` builds):
- `init_comms_distap(dis1_cb, dis2_cb)` — stores callbacks; no UART init
- Exposes `mac_distap_inject_frame(n_idx, display_type_t, display_data_t*)` — called by `SimFuelInjector` when the UI sends a pumping tick
- `mac_distap_inject_frame()` directly calls `dis1_cb` or `dis2_cb` with the data
- `distap_send_cmd()` — for the lifecycle commands (`TX_ID_SET_DISPLAY`, etc.) the Mac impl immediately constructs a valid `RX_ID_*` ACK response and returns `true` without any TCP round-trip (lifecycle commands are fire-and-forget in the simulator)

**`mac_cmd_distap.cpp`** (replaces `cmd_distap.c` in `FERP_SIMULATOR` builds):
- `distap_get_fw_version(ver)` — copies `"SIM_1.0.0"` into `ver`, returns `ESP_OK`
- `distap_set_display_type()`, `distap_set_err_mask()` — return `ESP_OK` immediately
- This makes `FuelDispTapDriver::_run_lifecycle()` skip FW update and go straight to RUNNING

#### UI → Simulator TCP envelope

The Python UI sends a pumping-tick command over the existing TCP JSON channel:

```json
{
  "cmd": "SIM_DISTAP_FRAME",
  "nozzle": 0,
  "display_type": 6,
  "flags": { "start_stop": 1 },
  "error": 0,
  "unit_price": 300,
  "total_price": 150,
  "volume_l": 500
}
```

`SimFuelInjector` (C++ side, inside `ferp-com-simulator`) receives this JSON, builds a
`display_data_t`, and calls `mac_distap_inject_frame()`.

Nozzle lift/replace is sent as:
```json
{ "cmd": "SIM_NOZZLE_INPUT", "nozzle": 0, "active": true }
```
`SimFuelInjector` calls `hsys_tog_button_press_event()` / `hsys_tog_button_release_event()`
on `ModuleFuel`'s internal `_nozzle[n]` via a public `inject_nozzle_event(n, active)` method.

---

### 3-I  Nozzle UI widget — redesign

The existing `nozzle_widget.py` (display-only) is **replaced** with a full control panel.

#### Controls per nozzle panel (×2: Nozzle 1, Nozzle 2)

```
┌─────────────────────────────────────────────────────┐
│  NOZZLE 1                                   ● IDLE  │
├─────────────────────────────────────────────────────┤
│  [▲ Nozzle UP / ▼ Nozzle DOWN]  ← toggle button    │
│                                                     │
│  Pump type:  [Sanki6-M1          ▼]  ← dropdown    │
│                 Sanki6-M1                           │
│                 Censtar6-M1                         │
│                 Censtar7-M1                         │
│                 Wayne6-M1                           │
│                 Longfeng8-M1                        │
│                                                     │
│  Unit price ($/L):  [ 3.00 ]   ← text, 2 d.p.     │
│  Pump rate (L/s):   [ 0.050 ]  ← text, 3 d.p.     │
│  Data send rate (/s): [ 10 ]   ← int, e.g. 10=100ms│
│                                                     │
│  Volume:     0.000 L                               │
│  Total:      $0.00                                  │
│  Unit price: $3.00                                  │
│                                                     │
│         [  PUMP  ]   ← momentary button            │
└─────────────────────────────────────────────────────┘
```

#### Behaviour logic

1. **Nozzle toggle** — sends `SIM_NOZZLE_INPUT {nozzle, active:true/false}` immediately on press. State shown in the status indicator (● IDLE → ● UP).

2. **PUMP button** (momentary) — only active when nozzle is UP. On click:
   - Validates unit price (float, ≥ 0.01) and pump rate (float, > 0) — shows error if invalid
   - Starts a repeating `after()` timer at interval = `1000 / data_send_rate` ms
   - Each tick:
     - Increments `volume += pump_rate / data_send_rate`
     - `total_price = volume × unit_price`
     - Builds `display_data_t` with `start_stop=1`, `unit_price×100`, `total_price×100`, `volume_l×1000`
     - Sends `SIM_DISTAP_FRAME` JSON over TCP
     - Updates the live volume/total display in the widget
   - Pressing PUMP again while pumping stops the timer (sends final frame with `start_stop=0` if desired, or just stops)
   - If nozzle is toggled DOWN while pumping, the timer stops automatically

3. **Display type dropdown** — value is sent with every `SIM_DISTAP_FRAME` as the `display_type` field

4. **Input validation**:
   - Unit price: must parse as float with 2 d.p., > 0
   - Pump rate: must parse as float with 3 d.p., > 0
   - Data send rate: must be integer ≥ 1
   - Invalid fields highlighted in red; PUMP button disabled

5. **Default values**: Sanki6-M1 / unit_price=3.00 / pump_rate=0.050 / send_rate=10

---

## 4. Sanki 6-Digit State Machine — detailedSP32 "display tap board") that physically taps the segment-display
bus of the dispenser.

---

## 2. Current Architecture (old firmware)

### 2-A  System topology

```
  ┌──────────────────────────────────────────────────────────────┐
  │  Fuel Dispenser                                              │
  │  ┌────────────┐    ┌────────────┐                           │
  │  │ Display 1  │    │ Display 2  │  (segment-display buses)  │
  │  └─────┬──────┘    └─────┬──────┘                           │
  └────────┼────────────────┼─────────────────────────────────┘
           │ UART tap        │ UART tap
  ┌────────▼────────────────▼──────────────────────────────────┐
  │  Display-Tap Board  (ESP07 / secondary ESP32)               │
  │  • Decodes segment display frames from both displays        │
  │  • Sends decoded data frames to Main ESP32 over UART        │
  └────────────────────────────┬───────────────────────────────┘
                               │  UART / serial frames
  ┌────────────────────────────▼───────────────────────────────┐
  │  Main ESP32 (this firmware)                                 │
  │                                                             │
  │  app_disptap   ← receives serial frames from DT board       │
  │      │  on_event callback (per-display data ready)          │
  │      ▼                                                       │
  │  app_fuel      ← processes decoded data + nozzle events     │
  │      │  on_event callback (APP_FUEL_EVENT_PUMPED)           │
  │      ▼                                                       │
  │  app_cloud / app_retransmit                                 │
  └─────────────────────────────────────────────────────────────┘
```

### 2-B  Module responsibilities

| Module | File(s) | Responsibility |
|---|---|---|
| `app_disptap` | `app_disptap.cpp/.h` | UART comms with DT board; decodes raw frames into `app_disptap_display_data_t`; fires per-frame callback to `app_fuel` |
| `app_fuel` | `app_fuel.cpp/.h` | Receives per-frame callbacks; queues data per nozzle; runs Sanki state machine; fires `APP_FUEL_EVENT_PUMPED` upward |
| `sanki_6_digit_1` | `pumps/sanki/sanki_6_digit_1.cpp/.h` | Pure algorithm: data validation, unit-price stabiliser, dual state machine (value-based + nozzle-based) |
| `app_retransmit` | `app_retransmit.cpp/.h` | Stores failed cloud sends to SD card via retransmission manager; retries on reconnect |

### 2-C  Data flow (single nozzle, steady-state)

```
DT board UART frame
    │
    ▼
com_distap / cmd_distap          (low-level UART framing)
    │
    ▼
_fuel_event_display_01/02()      (in app_disptap, per-display callback)
    │  fills app_disptap_display_data_t {type, unit_price, total_price, volume_l, flags}
    ▼
_on_ext_disptap_event()          (in app_fuel, per-frame callback)
    │  pushes into per-nozzle hsys_queue (depth=10)
    ▼
_app_process_display_data()      (polled from app_fuel_run() cooperative loop)
    │
    ├─► sanki6_process_data()     validate + correct total_price
    ├─► sanki6_data_validate()    stabilise unit_price across N frames
    └─► sanki6_process_state_machine()
            │  dual state machine:
            │  [value-based]  Unknown → Pumping → Pumping_Waiting
            │  [nozzle-based] Unknown → Pumping → Stopped_Waiting → Stopped
            │
            └─► is_pumped = true
                    │
                    ▼
                sanki6_get_event()   fill nozzle_event_t
                    │
                    ▼
                _on_event(APP_FUEL_EVENT_PUMPED, &ne)  → app_cloud / app_retransmit
```

### 2-D  Nozzle start/stop signalling

Physical nozzle lift/replace triggers a GPIO ISR → `hsys_tog_button_t` debounce →
sets an event-group bit → `app_fuel_run()` cooperative polling reads the bit and
updates `nozzle_state[n]` (a `bool` passed into `_app_process_display_data`).
The Sanki state machine uses this as `display_data->start_stop`.

### 2-E  Display-tap state machine

`app_disptap` runs its own lifecycle state machine:

```
wait_for_config_ready
    │  MsgConfigReady received
    ▼
disptap_waiting_for_reboot       (500 ms GPIO reset to DT board)
    │  timeout elapsed
    ▼
disptap_running                  (serial frames flowing)
```

During `waiting_for_reboot`: reads display type from config, sets it on DT board,
queries firmware version (published upward as `APP_ESP07_EVENT_FW_VERSION_LOADED`).

### 2-F  Key data types

```cpp
// Raw frame from DT board (filled by app_disptap, consumed by app_fuel)
typedef struct {
    display_type_t type;            // e.g. DIS_SANKI_6_DIGIT
    struct {
        flags_t  flags;             // .start_stop
        uint8_t  errors;
        uint32_t unit_price;        // × 100  (cents)
        uint32_t total_price;       // × 100  (cents)
        uint32_t volume_l;          // × 1000 (mL)
    } data;
} app_disptap_display_data_t;

// Final pumped event (filled by sanki6, passed to cloud)
typedef struct {
    uint8_t  n_idx;
    uint8_t  event_id;
    uint64_t time_stamp;            // epoch seconds
    uint32_t unit_pricex100;
    uint64_t total_pricex100;
    uint32_t volume_lx1000;
} nozzle_event_t;
```

### 2-G  Problems with old architecture

| # | Problem |
|---|---|
| 1 | `app_disptap` fires a **callback per frame** into `app_fuel` — tight coupling via function pointer table |
| 2 | `app_fuel` uses a cooperative `run()` poll loop + `hsys_queue` — no RTOS tasks, no message bus |
| 3 | Display-tap board lifecycle (boot, firmware check) is mixed into the same `app_disptap` file as data parsing |
| 4 | Nozzle state tracked via raw event-group bits — no clean start/stop message |
| 5 | `app_retransmit` is a separate app-level module with its own event table wiring — not cleanly separated from cloud |
| 6 | No simulator test path — impossible to verify state machine logic without real hardware |

---

## 3. Planned Architecture (HSYS message queue)

### 3-A  Key design decision — single `ModuleFuel` with internal sub-components

The display-tap data stream is **high-frequency and internal**: typically 10–30 frames
per second per nozzle while pumping.  Routing every frame through the HSYS message bus
as a `MsgDispTapData` would be wasteful (pool allocations, context switches, queue
overhead).

Instead, the DT serial driver calls a **direct C function** inside `ModuleFuel` from
its own task.  The only messages that cross the bus are **low-frequency events**:
lifecycle signals (config ready, SPIFFS ready), and the final `MsgFuelPumped`
(one per transaction).

**Nozzle GPIO inputs are also internal** — they are time-critical signals (lift/replace
of nozzle) where any message-bus latency would cause missed or delayed state transitions.
Following the pattern of the old `app_fuel`, each nozzle GPIO is debounced with a
`hsys_tog_button_t` pair registered directly inside `ModuleFuel`.  The toggle-button
callbacks set **event-group bits** that are consumed in `ModuleFuel`'s own task loop,
never routed through the bus.

```
  ModuleFuel owns:
  ┌────────────────────────────────────────────┐
  │  FuelDispTapDriver  (internal C++ class)   │
  │    • UART comms with DT board              │
  │    • DT board lifecycle state machine      │
  │    • Frame decoding per display            │
  │    • Direct call → FuelSankiProcessor      │
  │                                            │
  │  FuelSankiProcessor[2]  (per nozzle)       │
  │    • sanki6_process_data()                 │
  │    • sanki6_data_validate()                │
  │    • sanki6_process_state_machine()        │
  │    → calls ModuleFuel::on_pumped()         │
  └────────────────────────────────────────────┘
```

### 3-B  System topology (new)

```
  ┌──────────────────────────────────────────────────────────────┐
  │  Fuel Dispenser                                              │
  │  ┌────────────┐    ┌────────────┐                           │
  │  │ Display 1  │    │ Display 2  │                           │
  │  └─────┬──────┘    └─────┬──────┘                           │
  └────────┼────────────────┼─────────────────────────────────┘
           │ UART tap        │ UART tap
  ┌────────▼────────────────▼──────────────────────────────────┐
  │  Display-Tap Board  (ESP07 / secondary ESP32)               │
  └────────────────────────────┬───────────────────────────────┘
                               │  UART serial (com_distap / cmd_distap)
                               │  dis1_fuel_event / dis2_fuel_event callbacks
  ┌────────────────────────────▼───────────────────────────────┐
  │  ModuleFuel  (HSYS module, fuel_task)                       │
  │                                                             │
  │  subscribes:  MsgConfigReady                                │
  │  publishes:   MsgFuelPumped, MsgNozzleState, MsgDTFwVersion │
  │                                                             │
  │  internal:    FuelDispTapDriver  (UART via com_distap)      │
  │               FuelSankiProcessor[2]                         │
  │               hsys_tog_button_t nozzle[2]  ◄── GPIO ISR    │
  │                 GPIO ISR wakes fuel_task via event group     │
  └─────────────────────────────────────────────────────────────┘
           │ MsgFuelPumped
  ┌────────▼────────────────────────────────────────────────────┐
  │  ModuleCloud / ModuleRetransmit                             │
  └─────────────────────────────────────────────────────────────┘
```

### 3-C  HSYS message bus interactions + internal signal flows

```
  ┌──────────────────────────────────────────────────────────────────────────────┐
  │  ModuleConfig                                                                │
  └──────────────────────────────┬───────────────────────────────────────────────┘
          MsgConfigReady (bus)   │  NOTIFICATION
  ┌───────────────────────────── ▼──────────────────────────────────────────────┐
  │                          ModuleFuel                                         │
  │                                                                             │
  │  ── HSYS bus out ──────────────────────────────────────────────────────     │
  │     publishes ──►  MsgNozzleState    (NOTIFICATION, on nozzle up/down/done) │
  │     publishes ──►  MsgFuelPumped     (NOTIFICATION, on transaction complete)│
  │     publishes ──►  MsgDTFwVersion    (NOTIFICATION, after FW_UPDATE)        │
  │                                                                             │
  │  ── Internal signals (direct, no bus) ──────────────────────────────────    │
  │                                                                             │
  │  GPIO ISR                                                                   │
  │    board_register_cb_on_button_nozzle1_start/stop()                        │
  │    board_register_cb_on_button_nozzle2_start/stop()                        │
  │      └─► hsys_tog_button_press/release_event()                             │
  │            └─► sets event-group bits (NOZZLE1_START/STOP, NOZZLE2_START/STOP)
  │                  └─► wakes fuel_task (xEventGroupWaitBits unblocks)        │
  │                        └─► _processors[n].set_nozzle_state()               │
  │                                                                             │
  │  com_distap (UART serial receive task — separate FreeRTOS task)            │
  │    RX_ID_DIS1_DATA  ──►  dis1_fuel_event callback                          │
  │    RX_ID_DIS2_DATA  ──►  dis2_fuel_event callback                          │
  │      └─► _driver._on_frame_display1/2()                                    │
  │            └─► _processors[n].push_frame()  (direct call, no bus)          │
  │                  └─► state machine tick → on_pumped() if transaction done  │
  │                                                                             │
  │  cmd_distap (called during FW_UPDATE lifecycle only)                       │
  │    distap_get_fw_version() ─► compare vs config                            │
  │    distap_set_display_type()  ─► on RUNNING entry                          │
  │    distap_set_err_mask()      ─► on RUNNING entry                          │
  └─────────────────────────────────────────────────────────────────────────────┘
```

**Task wake mechanism**: `ModuleFuel::run()` blocks on `xEventGroupWaitBits()` with a
timeout so it also runs periodic processor ticks. GPIO ISR wakes it immediately when a
nozzle state changes. The `com_distap` receive task feeds frames via direct callbacks
into `FuelDispTapDriver` from its own stack — no wake needed (always running).

**Nozzle inputs are internal to `ModuleFuel`** — they are time-critical and cannot
tolerate message-bus latency.  Following the exact pattern of old `app_fuel`:
- `hsys_tog_button_t nozzle_1, nozzle_2` are members of `ModuleFuel`
- GPIO ISR fires → `hsys_tog_button_press/release_event()` → sets event bits on `_fuel_event` group
- `ModuleFuel` task loop checks event bits each iteration: sets `nozzle_state[n]` and calls `_processors[n].set_nozzle_state()`
- **Exception**: Sanki frames embed `start_stop` flag directly — the GPIO path is still registered but the processor may ignore the external signal and use the frame flag instead.

### 3-D  New messages

All IDs are assigned in the `0x0800` block (fueling domain):

| Message | ID | Direction | Type | Payload |
|---|---|---|---|---|
| `MsgFuelPumped` | `0x0800` | ModuleFuel → all | NOTIFICATION | `n_idx`, `unit_pricex100`, `total_pricex100`, `volume_lx1000`, `time_stamp` |
| `MsgNozzleState` | `0x0801` | ModuleFuel → all | NOTIFICATION | `n_idx`, `state` (IDLE/PUMPING/PUMPED) |
| `MsgDTFwVersion` | `0x0802` | ModuleFuel → all | NOTIFICATION | `version[32]` (string), `board_type` (ESP07/ESP32) |

> `MsgDispTapData` (raw frame) is **intentionally absent from the bus** — it is an
> internal call within `ModuleFuel`.  Nozzle start/stop GPIO events are also **not on
> the bus** — they are handled internally via `hsys_tog_button_t` + event-group bits,
> matching the old `app_fuel` design.  This keeps bus traffic low and pool usage minimal.

### 3-E  File structure

```
src/
  app-messages/
    msg_fuel_pumped.h / .cpp          (ID 0x0800)
    msg_nozzle_state.h / .cpp         (ID 0x0801)
    msg_dt_fw_version.h / .cpp        (ID 0x0802)
    messages/
      Fuel/
        msg_fuel_pumped.json
        msg_nozzle_state.json
        msg_dt_fw_version.json

  app-modules/
    module_fuel/
      module_fuel.h                   ← HsysModule subclass
      module_fuel.cpp                 ← subscribe, message dispatch, on_pumped(),
                                         nozzle event-group handling (hsys_tog_button_t)
      fuel_disptap_driver.h           ← FuelDispTapDriver class (internal)
      fuel_disptap_driver.cpp         ← DT board lifecycle, FW update, UART framing
      pumps/                          ← one subfolder per display type
        fuel_pump_types.h             ← display_type_t enum + common types
        nozzle_event.h                ← nozzle_event_t (product-agnostic)
        sanki/
          fuel_sanki_processor.h      ← FuelSankiProcessor class
          fuel_sanki_processor.cpp    ← ported sanki6 algorithms
        wayne/                        ← stub (Wayne 6-digit)
        censtar/                      ← stub (Censtar 6/7-digit)
        longfeng/                     ← stub (Longfeng 8-digit)

  product/
    app/
      fuel_config.h                   ← FUEL_MAX_NOZZLES=2, display type config

  sub-modules/pal/mac-pc/driver/
    mac_com_distap.cpp                ← simulator impl of com_distap.h (see §3-H)
    mac_cmd_distap.cpp                ← simulator impl of cmd_distap.h (see §3-H)
    mac_serial_flasher.cpp            ← no-op stub for serial_flasher.h (see §3-G)

tools/sim-ui/widgets/
  nozzle_widget.py                    ← EXTENDED: nozzle control panel (see §3-I)
  spiffs_widget.py                    ← NEW: SPIFFS explorer tab (see §3-J)
```

### 3-F  Internal class design

```
class ModuleFuel : public HsysModule
├── init()            subscribe MsgConfigReady
│                     register board GPIO callbacks → hsys_tog_button_t nozzle[2]
│                       board_register_cb_on_button_nozzle1_start/stop()
│                       board_register_cb_on_button_nozzle2_start/stop()
├── on_msg_received() dispatch MsgConfigReady → _driver.on_config_ready()
├── run()             check _fuel_event event-group bits each iteration:
│                       NOZZLE1_START/STOP → nozzle_state[0]; call _processors[0].set_nozzle_state()
│                       NOZZLE2_START/STOP → nozzle_state[1]; call _processors[1].set_nozzle_state()
│                       (mirrors old app_fuel_run() pattern exactly)
├── on_pumped(n_idx)  fill + publish MsgFuelPumped, MsgNozzleState(PUMPED)
│
├── hsys_tog_button_t  _nozzle[2]      (debounced GPIO, internal — no bus)
│     callbacks set event bits on _fuel_event group
│     debounce: 500ms press / 500ms release  (same as old app_fuel)
│
├── hsys_eventgroup_handle_t  _fuel_event
│     bits: NOZZLE1_START | NOZZLE1_STOP | NOZZLE2_START | NOZZLE2_STOP
│
├── FuelDispTapDriver  _driver
│   ├── on_config_ready()     read display_type from config, select DT board type
│   ├── _run_lifecycle()      WAIT_CONFIG → RESETTING → FW_UPDATE → RUNNING
│   │     FW update:  checks DT board type (ESP07 vs ESP32), runs serial_flasher
│   │     if update needed before entering RUNNING state
│   ├── _on_frame_display1()  decode frame → _processors[0].push_frame()
│   └── _on_frame_display2()  decode frame → _processors[1].push_frame()
│
└── pumps/FuelSankiProcessor  _processors[FUEL_MAX_NOZZLES]
    ├── push_frame()           direct call from driver
    ├── set_nozzle_state()     direct call from ModuleFuel run() on event-group bit
    │                          (for Sanki: frame's start_stop flag takes precedence)
    └── process()              tick: validate → stabilise → state machine
                               → callback ModuleFuel::on_pumped()
```

**DT board firmware update flow** (inside `FuelDispTapDriver::_run_lifecycle`):

```
WAIT_CONFIG
    │ MsgConfigReady
    ▼
RESETTING         GPIO reset pulse to DT board (500 ms)
    │ timeout
    ▼
FW_UPDATE         Query DT board FW version via serial
    │             Compare against expected version in config
    │             If mismatch → run serial_flasher
    │               - ESP07 board type  → serial_flasher_esp07()
    │               - ESP32 board type  → serial_flasher_esp32()
    │             Publish MsgDTFwVersion once version confirmed
    ▼
RUNNING           Normal frame decoding active
```

---

### 3-G  Submodule interfaces used by `FuelDispTapDriver`

These three headers are consumed from the `ferp-device-firmware` submodule
(`src/sub-modules/ferp-device-firmware/ferp_board/main-esp32/lib/`).  
They are **ESP-IDF only** — guarded out in the simulator build.

#### `display_types.h`
Core type definitions shared between the DT board firmware and the host:

| Type | Description |
|---|---|
| `display_type_t` | Enum of all supported pump display brands/sizes (`DIS_NONE`, `DIS_SANKI_6_DIGIT`, `DIS_WAYNE_6_DIGIT`, `DIS_CENSTAR_6_DIGIT`, `DIS_CENSTAR_7_DIGIT`, `DIS_LONGFENG_8_DIGIT`, …) |
| `display_data_t` | Packed struct per frame: `flags` (`start_stop`, `select_p/l/ll`), `error` bitmask, `total_price` (×0.01), `volume_l` (×0.001), `unit_price` (×0.01) |
| `data_flags_t` | Bit-field union inside `display_data_t`; `start_stop` bit is the embedded nozzle-lift signal for pump types like Sanki |
| `data_error_t` | Bit-field union: `index`, `unitprice`, `totprice`, `volume`, `price_gap` error flags |
| `input_pin_t` | GPIO input state union (two display ports, 4 SPI-like pins each) |

> **Note**: `display_type_t` from this header is the authoritative enum.  
> `fuel_pump_types.h` inside `module_fuel/pumps/` wraps or re-uses it — do **not** redefine the enum locally.

#### `com_distap.h`
Low-level UART framing layer (C API):

```c
// Packet structure (packed, variable-length):
// pck_id | display (display_type_t) | length | data[] | CRC16
typedef union { struct { uint8_t pck_id; uint8_t display; uint8_t length; uint8_t ab_data[]; }; uint8_t ab_raw[]; } data_packet_t;

// Packet IDs:
// TX (host → DT board): TX_ID_DEV_INFO, TX_ID_SET_DISPLAY, TX_ID_SET_ERR_MASK, TX_ID_INPUTS
// RX (DT board → host): RX_ID_DIS1_DATA, RX_ID_DIS2_DATA, RX_ID_DEV_INFO,
//                        RX_ID_SET_DISPLAY, RX_ID_SET_ERR_MASK, RX_ID_INPUTS,
//                        RX_ID_KEEP_ALIVE, RX_ID_LOG_PRINTS, RX_ID_NACK

// Initialise UART comms; registers two callbacks for nozzle data events:
esp_err_t init_comms_distap(
    void (*dis1_fuel_event)(display_type_t type, uint8_t *data),
    void (*dis2_fuel_event)(display_type_t type, uint8_t *data));

void suspend_comms_distap();
void resume_comms_distap();
bool distap_send_cmd(data_packet_t *rx, data_packet_t *tx, uint32_t tout);
```

`FuelDispTapDriver` calls `init_comms_distap()` in the `RUNNING` state, passing
`_on_frame_display1` and `_on_frame_display2` as the fuel-event callbacks.

#### `cmd_distap.h`
Higher-level command layer built on top of `com_distap` (C API):

```c
esp_err_t distap_get_fw_version(char *ver);      // used in FW_UPDATE lifecycle step
esp_err_t distap_set_display_type(display_type_t); // set after config is ready
esp_err_t distap_get_inputs(input_pin_t *pins);  // polled in RUNNING (optional)
esp_err_t distap_set_inputenable(bool level);
esp_err_t distap_set_led_enable(bool level);
esp_err_t distap_set_cs1_enable(bool level);
esp_err_t distap_set_cs2_enable(bool level);
esp_err_t distap_set_err_mask(data_error_t err);
```

`FuelDispTapDriver::_run_lifecycle()` uses:
1. `distap_get_fw_version()` — compare against expected version from config
2. `distap_set_display_type()` — called once when entering `RUNNING`
3. `distap_set_err_mask()` — configure which frame errors are fatal

#### `serial_flasher.h`
ESP-IDF Espressif serial flasher library (wraps `esp_loader`):

```c
// Entry point — called only if FW version mismatch detected:
void start_serial_flash(bool skip_version_check);

// Lower-level helpers (used internally by start_serial_flash):
esp_loader_error_t connect_to_target(uint32_t higher_transmission_rate);
esp_loader_error_t flash_binary(const char *file_name, size_t size, size_t address);
```

Compile-time board selection (defined in `fuel_config.h` or CMakeLists):
- `#define DISTAP_ESP07` → reads firmware from `/spiffs/esp07/`, flashes bootloader @`0x00`, partition @`0x8000`, app @`0x10000`
- `#define DISTAP_ESP32` → reads firmware from `/spiffs/esp32/`, flashes bootloader @`0x1000`, partition @`0x8000`, app (path TBD)

`FuelDispTapDriver` calls `start_serial_flash(false)` when version mismatch is detected in the `FW_UPDATE` state.

#### Simulator build guards

All four headers are ESP-IDF specific and are handled differently in the simulator:

| Header | Simulator strategy |
|---|---|
| `com_distap.h` | Replaced by `mac_com_distap.cpp` in `pal/mac-pc/driver/` (see §3-H) |
| `cmd_distap.h` | Replaced by `mac_cmd_distap.cpp` in `pal/mac-pc/driver/` (see §3-H) |
| `serial_flasher.h` | Replaced by a **no-op stub** `mac_serial_flasher.cpp` — `start_serial_flash()` returns immediately; `FW_UPDATE` state is bypassed entirely since `mac_cmd_distap` already returns `"SIM_1.0.0"` for `distap_get_fw_version()`, so the version always matches and flashing is never triggered |
| `board_inf.h` (GPIO reset) | `gpio_set_reset_distap()` is a no-op stub in the Mac PAL — the RESETTING state completes after its 500 ms timeout with no real GPIO toggled |

**`serial_flasher` is never called in the simulator** because:
1. `mac_cmd_distap::distap_get_fw_version()` always returns `"SIM_1.0.0"`
2. `FuelDispTapDriver::_run_lifecycle()` compares that against the config expected version
3. If they match → skip `start_serial_flash()` entirely, proceed to `RUNNING`
4. `mac_serial_flasher.cpp` only exists as a safety net in case the guard logic ever changes

**`mac_serial_flasher.cpp`** (`pal/mac-pc/driver/`):
```cpp
// Simulator stub — serial flashing is not possible on Mac
void start_serial_flash(bool skip_version_check) {
    printf("[SIM] start_serial_flash() called — no-op in simulator\n");
}
esp_loader_error_t connect_to_target(uint32_t rate)   { return ESP_LOADER_SUCCESS; }
esp_loader_error_t flash_binary(const char*, size_t, size_t) { return ESP_LOADER_SUCCESS; }
esp_loader_error_t load_ram_binary(const uint8_t*)    { return ESP_LOADER_SUCCESS; }
```

---

### 3-J  SPIFFS explorer UI tab

The SPIFFS folder used by the simulator is:
```
src/product/ferp-com-simulator/SPIFFS/spiffs/
```
This is the same path that `config_widget.py` already knows about (it reads
`Configs/DeviceConfigs.json` from there).  The DT board firmware binaries will
also be placed here for the FW update path:
```
src/product/ferp-com-simulator/SPIFFS/spiffs/
  Configs/
    DeviceConfigs.json
  esp07/                ← DT board binaries for ESP07 target
    bootloader.bin
    partitions_table.bin
    rtos_dis_tap_esp07.bin
  esp32/                ← DT board binaries for ESP32 target
    bootloader.bin
    partition_table.bin
    distap_esp32.bin
```

#### New tab: "SPIFFS"

A new `spiffs_widget.py` is added to `tools/sim-ui/widgets/` and registered in
`sim_ui.py` as a tab between "Config" and "Messages".

```
┌────────────────────────────────────────────────────────────┐
│  SPIFFS Explorer                        [⟳ Refresh]        │
│  Root: src/product/ferp-com-simulator/SPIFFS/spiffs/       │
│  [Browse...]                                               │
├────────────────────────────────────────────────────────────┤
│  📁 Configs/                                  (dir)        │
│     📄 DeviceConfigs.json              1.2 KB  [View]      │
│  📁 esp07/                                    (dir)        │
│     📄 bootloader.bin                 12.3 KB  [—]         │
│     📄 partitions_table.bin            3.1 KB  [—]         │
│     📄 rtos_dis_tap_esp07.bin        512.0 KB  [—]         │
│  📁 esp32/                                    (dir)        │
│     📄 bootloader.bin                 15.0 KB  [—]         │
│     📄 partition_table.bin             3.1 KB  [—]         │
│     📄 distap_esp32.bin              620.0 KB  [—]         │
├────────────────────────────────────────────────────────────┤
│  Used: 1.2 KB of text files  ·  Binary: 3 files            │
└────────────────────────────────────────────────────────────┘
```

#### Behaviour

- **Tree view**: `ttk.Treeview` with two columns — filename and size. Directories are collapsible. All files under the SPIFFS root are shown recursively.
- **Refresh button**: rescans the directory tree from disk.
- **Browse button**: opens a folder picker so the user can point to a different SPIFFS root (useful when testing against a different product config).
- **[View] button** (JSON files only): opens the file content in a read-only scrolled text pop-up (same dark theme). Mirrors the behaviour already in `config_widget.py`.
- **Binary files** (`.bin`): shown with size only, no View button.
- **Root path**: defaults to the repo-relative SPIFFS path, computed the same way as in `config_widget.py`.
- **sim_ui.py change**: add `from widgets.spiffs_widget import SpiffsWidget` and one `notebook.add()` call — no other changes needed.

---

## 4. Sanki 6-Digit State Machine — detailed

The Sanki state machine is the most complex piece and must be ported unchanged
in behaviour. Here is the logic as it exists:

### 4-A  Data pipeline per frame

```
push_frame(raw_frame)
    │
    ▼
sanki6_process_data()
    Validates unit_price > 0
    Corrects total_price when leading digits are missing (segment display artifact)
    Rejects frames where vol=0 & total>0, or vol>0 & total=0
    Returns: is_valid
    │
    ▼  (only if is_valid)
sanki6_data_validate()
    Runs unit_price through stabiliser (requires N=10 consecutive same values)
    Copies validated values into display_data_validated[]
    │
    ▼
sanki6_process_state_machine()
    Runs two independent state machines:
```

### 4-B  Dual state machine

```
VALUE-BASED state machine  (driven by derivative of total_price)
┌─────────┐  deriv > 0     ┌──────────────┐
│ Unknown ├───────────────►│   Pumping    │
└─────────┘                └──────┬───────┘
     ▲                    no data │ > 500ms
     │                            ▼
     │              ┌─────────────────────────┐
     │  deriv > 0   │  Pumping_Waiting        │
     └──────────────┴─────────────────────────┘

NOZZLE-BASED state machine  (driven by start_stop flag from nozzle button)
┌─────────┐  start_stop=1  ┌──────────────┐
│ Unknown ├───────────────►│   Pumping    │
└─────────┘                └──────┬───────┘
     ▲                  stop AND  │ increment_count >= 10
     │                 pump time  ├──► commit_data()
     │                > 4s        ▼
     │              ┌──────────────────────┐
     │              │   Stopped_Waiting    │
     │              └──────┬───────────────┘
     │        value state  │ = Pumping_Waiting
     │        AND > 100ms  ▼
     │              ┌──────────────────────┐
     │              │      Stopped         │──► is_pumped_event = true
     │              └──────────────────────┘
     └───────────────────────────┘ (nozzle_based → Unknown)
```

**Edge case**: if nozzle stop happens with `increment_count < 10` AND within 4 s —
treated as false trigger, state resets to Unknown without emitting event.

### 4-C  Unit-price stabiliser

The segment display can flicker between two values while updating.
The stabiliser requires `DISPLAY_SAME_COUNT_FOR_STABILIZE_UNIT_PRICE = 10`
consecutive identical readings before accepting a new unit price.

---

## 5. Simulator Test Plan

### 5-A  Strategy

The DT board is emulated entirely by the **Python UI** (see §3-H and §3-I).  
The UI is responsible for:
1. Accepting user inputs (nozzle state, pump type, unit price, pump rate, send rate)
2. Computing `display_data_t` field values from those inputs on every tick
3. **Encoding the payload into the exact same `data_packet_t` wire format** that a real
   DT board would transmit over UART
4. Sending that encoded packet as a `SIM_DISTAP_FRAME` JSON envelope over TCP to the
   simulator process
5. `SimFuelInjector` (C++ side) decodes the JSON, reconstructs `display_data_t`, and
   calls `mac_distap_inject_frame()` — which fires `dis1_fuel_event` or `dis2_fuel_event`
   directly into `FuelDispTapDriver`, exactly as if the byte arrived from UART

This means `FuelDispTapDriver`, `FuelSankiProcessor`, and `ModuleFuel` are exercised
through exactly the same code path in the simulator as on real hardware.  There is no
separate "scripted injector" — the UI **is** the DT board.

### 5-B  Data flow (end-to-end)

```
  Python UI (nozzle_widget.py)
  │
  │  User sets: pump_type, unit_price, pump_rate, send_rate
  │  User presses: Nozzle UP toggle
  │  User presses: PUMP button
  │
  │  Every (1000 / send_rate) ms:
  │    volume      += pump_rate / send_rate
  │    total_price  = volume × unit_price
  │
  │    Build display_data_t:
  │      flags.start_stop = 1
  │      unit_price  = round(unit_price × 100)   as uint32_t
  │      total_price = round(total_price × 100)  as uint32_t
  │      volume_l    = round(volume × 1000)       as uint32_t
  │      error       = 0x00
  │
  │    Send JSON over TCP:
  │    {
  │      "cmd": "SIM_DISTAP_FRAME",
  │      "nozzle": 0,
  │      "display_type": 6,          ← DIS_SANKI_6_DIGIT enum value
  │      "flags": 1,                 ← start_stop bit set
  │      "error": 0,
  │      "unit_price": 300,          ← ×100 integer
  │      "total_price": 150,         ← ×100 integer
  │      "volume_l": 500             ← ×1000 integer
  │    }
  │
  ▼
  SimFuelInjector (C++, ferp-com-simulator)
    Receives JSON → fills display_data_t
    Calls mac_distap_inject_frame(nozzle, display_type, &data)
  │
  ▼
  mac_com_distap.cpp
    Calls dis1_fuel_event(display_type, (uint8_t*)&data)
  │
  ▼
  FuelDispTapDriver::_on_frame_display1()
    Calls _processors[0].push_frame(&data)
  │
  ▼
  FuelSankiProcessor::push_frame() → process()
    State machine runs on real algorithm
  │
  ▼  (when transaction completes)
  ModuleFuel::on_pumped()
    Publishes MsgFuelPumped + MsgNozzleState(PUMPED)
  │
  ▼
  ModuleSimBridge → sends JSON to Python UI
    UI nozzle_widget updates state indicator + final values
```

### 5-C  Nozzle input flow

```
  Python UI
    User toggles Nozzle UP/DOWN
    Sends: { "cmd": "SIM_NOZZLE_INPUT", "nozzle": 0, "active": true }
  │
  ▼
  SimFuelInjector
    Calls ModuleFuel::inject_nozzle_event(0, true)
  │
  ▼
  ModuleFuel (internal)
    Calls hsys_tog_button_press_event(&_nozzle[0])
    Sets NOZZLE1_START bit in _fuel_event group
    Wakes fuel_task → nozzle_state[0] = true
    Calls _processors[0].set_nozzle_state(true)
```

### 5-D  Test scenarios (performed manually via the UI)

| # | Scenario | Steps | Expected result |
|---|---|---|---|
| 1 | **Normal transaction — Sanki** | Select Sanki6-M1, unit=3.00, rate=0.050 L/s, send=10/s. Toggle Nozzle 1 UP. Press PUMP for ~5 s. Toggle Nozzle 1 DOWN. | `MsgFuelPumped` published with volume≈0.250 L, total≈0.75, unit=300. UI shows PUMPED state then returns to IDLE. |
| 2 | **Nozzle UP but no pump** | Toggle Nozzle 1 UP. Do not press PUMP. Wait 10 s. | No `MsgFuelPumped`. State stays IDLE (no frames sent = no state machine transition). |
| 3 | **Noisy unit price (Sanki stabiliser)** | In unit price field enter a value, but modify the `SIM_DISTAP_FRAME` JSON to send alternating unit_price values for first 9 frames, stable from frame 10. | `MsgFuelPumped` shows the stabilised (correct) unit price, not the noisy early values. |
| 4 | **Two nozzles simultaneously** | Nozzle 1 UP + Nozzle 2 UP. PUMP both. | Two independent `MsgFuelPumped` events, one per nozzle, with correct individual totals. |
| 5 | **DT board lifecycle (sim)** | Start simulator. | `FuelDispTapDriver` transitions WAIT_CONFIG → RESETTING → FW_UPDATE → RUNNING with no crash; `MsgDTFwVersion` published with `version="SIM_1.0.0"`. |

### 5-E  Message JSON descriptors

Add to `src/app-messages/messages/Fuel/`:

```json
// msg_fuel_pumped.json
{
    "msg_id": 2048,
    "name": "MsgFuelPumped",
    "direction": "NOTIFICATION",
    "fields": [
        { "name": "n_idx",           "type": "uint8_t" },
        { "name": "unit_pricex100",  "type": "uint32_t" },
        { "name": "total_pricex100", "type": "uint32_t" },
        { "name": "volume_lx1000",   "type": "uint32_t" },
        { "name": "time_stamp",      "type": "uint64_t" }
    ]
}
// msg_nozzle_state.json
{
    "msg_id": 2049,
    "name": "MsgNozzleState",
    "direction": "NOTIFICATION",
    "fields": [
        { "name": "n_idx",  "type": "uint8_t" },
        { "name": "state",  "type": "uint8_t",  "comment": "0=IDLE 1=PUMPING 2=PUMPED" }
    ]
}
// msg_dt_fw_version.json
{
    "msg_id": 2050,
    "name": "MsgDTFwVersion",
    "direction": "NOTIFICATION",
    "fields": [
        { "name": "version",    "type": "char[32]" },
        { "name": "board_type", "type": "uint8_t",  "comment": "0=ESP07 1=ESP32" }
    ]
}
```

---

## 6. Implementation Steps

### Step 1 — Message definitions  ⬜
- Add `MSG_ID_FUEL_PUMPED = 0x0800`, `MSG_ID_NOZZLE_STATE = 0x0801`,
  `MSG_ID_DT_FW_VERSION = 0x0802` to `app_msg_ids.h`
- Create `msg_fuel_pumped.h/.cpp`, `msg_nozzle_state.h/.cpp`, `msg_dt_fw_version.h/.cpp`
- Add JSON descriptor files under `src/app-messages/messages/Fuel/` (3 files, no `MsgNozzleInput`)
- Add `from_json()` implementations (for inject widget)

### Step 2 — Port Sanki algorithm  ⬜
- Create `pumps/fuel_pump_types.h` — re-use `display_type_t` from submodule `display_types.h`; define `app_display_data_t` wrapping `display_data_t`
- Create `pumps/nozzle_event.h` — `nozzle_event_t` struct
- Create `fuel_config.h` under `src/product/app/` — `#define FUEL_MAX_NOZZLES 2` + `DISTAP_ESP07` or `DISTAP_ESP32`
- Create `pumps/sanki/FuelSankiProcessor` class wrapping ported `sanki6_*` functions; frame input type is `display_data_t*`
- Create stub processor headers for `wayne/`, `censtar/`, `longfeng/`
- Unit-test sanki processor with scripted frame arrays in `test_sanki.cpp`

### Step 3 — `FuelDispTapDriver`  ⬜
- Create `FuelDispTapDriver` with full lifecycle: `WAIT_CONFIG → RESETTING → FW_UPDATE → RUNNING`
- **FW_UPDATE path** (ESP32-IDF only):
  1. Call `distap_get_fw_version(ver)` — compare against config expected version
  2. If mismatch: call `start_serial_flash(false)` (board type selected by `DISTAP_ESP07`/`DISTAP_ESP32` compile flag)
  3. Publish `MsgDTFwVersion` with confirmed version string + board type
- **RUNNING entry**: call `distap_set_display_type(type)`, `distap_set_err_mask(err)`, then `init_comms_distap(_on_frame_display1, _on_frame_display2)`
- **Simulator build** (`#ifndef FERP_SIMULATOR`): `_run_lifecycle()` skips all `distap_*` / `start_serial_flash` calls; frame callbacks wired via `inject_frame()`
- **ESP32-IDF build**: `_run_lifecycle()` runs full lifecycle including FW update

### Step 4 — `ModuleFuel` skeleton  ⬜
- Create `ModuleFuel : HsysModule` with `MODULE_FUEL_ID = 11`
- Subscribe `MsgConfigReady` only
- Create `_fuel_event` event group; register nozzle GPIO callbacks via
  `board_register_cb_on_button_nozzle1/2_start/stop()` → `hsys_tog_button_t _nozzle[2]`
- Register in `app.cpp` module and task tables
- Simulator builds, no crash

### Step 5 — Wire Sanki processor into module  ⬜
- On valid frame from driver → `_processors[n].push_frame()`
- In `run()` loop: check `_fuel_event` bits → `_processors[n].set_nozzle_state()`
  (for Sanki: processor also reads embedded `start_stop` flag from frame directly)
- `on_pumped(n_idx)` → publish `MsgFuelPumped` + `MsgNozzleState(PUMPED)`

### Step 6 — Simulator DispTap emulation layer  ⬜
- Create `mac_com_distap.cpp` — implements `init_comms_distap()`, `distap_send_cmd()`,
  and `mac_distap_inject_frame(n_idx, type, data*)` (see §3-H for full spec)
- Create `mac_cmd_distap.cpp` — `distap_get_fw_version()` returns `"SIM_1.0.0"`;
  all other `distap_*` commands return `ESP_OK` immediately
- Create `mac_serial_flasher.cpp` — no-op stub; `start_serial_flash()` prints a log
  line and returns immediately; `connect_to_target()` / `flash_binary()` return success
  (see §3-G for rationale — flashing is never triggered because version always matches)
- Add `SimFuelInjector` to `ferp-com-simulator` — receives `SIM_DISTAP_FRAME` JSON
  over TCP, decodes to `display_data_t`, calls `mac_distap_inject_frame()`
- `SimFuelInjector` also handles `SIM_NOZZLE_INPUT` JSON → calls
  `ModuleFuel::inject_nozzle_event(n, active)` which calls `hsys_tog_button_press/release_event()`
- Add all three new Mac PAL files to `ferp-com-simulator/CMakeLists.txt`;
  exclude `serial_flasher.c`, `com_distap.c`, `cmd_distap.c` from the simulator build

### Step 7 — Nozzle UI widget  ⬜
- Rewrite `tools/sim-ui/widgets/nozzle_widget.py` as full control panel (see §3-I):
  - Two independent nozzle panels (Nozzle 1, Nozzle 2)
  - Nozzle toggle button → sends `SIM_NOZZLE_INPUT` JSON
  - Pump type dropdown (Sanki6-M1, Censtar6-M1, Censtar7-M1, Wayne6-M1, Longfeng8-M1)
  - Unit price (2 d.p.), pump rate L/s (3 d.p.), data send rate (int/s) with validation
  - PUMP button → starts repeating timer, increments volume, sends `SIM_DISTAP_FRAME` every N ms
  - Live volume/total display updates during pumping
- Create `tools/sim-ui/widgets/spiffs_widget.py` — SPIFFS explorer tab (see §3-J):
  - `ttk.Treeview` showing full recursive directory tree under SPIFFS root
  - Refresh, Browse, file size column, [View] for JSON files
- Register both widgets in `sim_ui.py`:
  - Nozzles tab: rename labels from "Nozzle 0/1" → "Nozzle 1/2"
  - Add SPIFFS tab between Config and Messages

### Step 8 — Verify all scenarios in simulator  ⬜
- Scenario 1: normal pump → `MsgFuelPumped` values match UI-entered totals
- Scenario 2: nozzle up without pumping → no spurious event
- Scenario 3: noisy unit price → correct stable value in final event
- Scenario 4: two nozzles simultaneously → two independent `MsgFuelPumped` events
- Scenario 5: DT board lifecycle → `MsgDTFwVersion` published with `"SIM_1.0.0"`

### Step 9 — ESP32-IDF build verification  ⬜
- `idf.py build` passes with no new errors
- `FuelDispTapDriver` real UART path compiles (real `com_distap.c` / `cmd_distap.c` included)
- `serial_flasher` compiles for IDF; `mac_serial_flasher.cpp` excluded from IDF build
- SPIFFS `esp07/` or `esp32/` folder populated with DT board binaries for FW update path

---

## 7. Resolved Design Decisions

| # | Question | Decision |
|---|---|---|
| 1 | **Nozzle input source** | **Internal to `ModuleFuel` — not on the bus.** Nozzle start/stop are time-critical GPIO signals. `ModuleFuel` owns two `hsys_tog_button_t _nozzle[2]` members, registered via `board_register_cb_on_button_nozzle1/2_start/stop()`. Callbacks set event-group bits; `run()` loop reads bits and calls `_processors[n].set_nozzle_state()` directly. This mirrors old `app_fuel` exactly. No `MsgNozzleInput`, no `ModuleNozzleInput`. |
| 2 | **Number of nozzles** | Fixed at **2** for now. Defined as `FUEL_MAX_NOZZLES 2` in `src/product/app/fuel_config.h`. |
| 3 | **Display types** | Multiple display types are supported. All display-type-specific files live under `module_fuel/pumps/<type>/`. Currently active: Sanki 6-digit. Stubs kept for Wayne, Censtar, Longfeng. |
| 4 | **DT board FW update** | **Required and in scope.** Two board types: **ESP07** and **ESP32** (selected at compile time via `DISTAP_ESP07`/`DISTAP_ESP32`). `FuelDispTapDriver` lifecycle includes a `FW_UPDATE` state: calls `distap_get_fw_version()`, then `start_serial_flash(false)` on mismatch. Firmware binaries loaded from SPIFFS (`/spiffs/esp07/` or `/spiffs/esp32/`). |
| 5 | **Retransmit integration** | `ModuleRetransmit` is a **separate future module** that subscribes to a missed/failed event message. `ModuleFuel` has no direct coupling to retransmit — it only publishes `MsgFuelPumped`. |
| 6 | **`NO_NOZZELS` / nozzle count constant** | Application-specific. Lives in `src/product/app/fuel_config.h` as `FUEL_MAX_NOZZLES`. |

---

## 8. Message ID registry additions

These IDs must be added to `app_msg_ids.h` at the start of Sprint 4:

```cpp
// ------------------------------------------------------------------
// Fueling  (0x0800 – 0x08FF)
// ------------------------------------------------------------------
MSG_ID_FUEL_PUMPED      = 0x0800,   ///< ModuleFuel -> all: transaction complete
MSG_ID_NOZZLE_STATE     = 0x0801,   ///< ModuleFuel -> all: nozzle lifted/replaced/pumped
MSG_ID_DT_FW_VERSION    = 0x0802,   ///< ModuleFuel -> all: DT board FW version confirmed
// NOTE: No MSG_ID_NOZZLE_INPUT — nozzle GPIO is handled internally via
//       hsys_tog_button_t + event-group bits inside ModuleFuel (time-critical path).
```
