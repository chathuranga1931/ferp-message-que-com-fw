# ModuleSimBridge (Driver)

**Location:** `src/sub-modules/pal/mac-pc/driver/`  
**Files:** `module_sim_bridge.h`, `module_sim_bridge.cpp`  
**Platform:** macOS simulator only  
**Status:** ✅ Implemented

## Purpose

Acts as the HSYS module inside the macOS simulator process that bridges the
message bus to the Python UI over TCP. It serialises outgoing `SIM_*` JSON
messages, forwards incoming JSON commands from the UI as `SIM_*` messages on
the bus, and drives the TCP engine (`MacDriver`).

## Role in the simulator stack

```
Python UI (sim_ui.py)
        │  TCP port 9000 (JSON lines)
        ▼
  MacDriver (tcp_engine)           ← mac_driver.h/.cpp
        │
  ModuleSimBridge                  ← module_sim_bridge.h/.cpp
        │  HSYS message bus
        ▼
  App modules (config, spiffs, leds …)
```

## Messages

| Direction | Message | Notes |
|-----------|---------|-------|
| Subscribes | `SIM_GPIO_OUT` | Published by `pal_mac_gpio` — forwards to TCP |
| Subscribes | `SIM_SPIFFS_READY` | *(future)* |
| Publishes  | `SIM_BUTTON_PRESS` | Injected from Python UI → injected into bus |

Internally `MacDriver` calls `module_sim_bridge_on_rx(json_line)` for every
complete JSON line received from the UI socket.

## JSON protocol

Every message is a single UTF-8 JSON line terminated with `\n`:

```json
{ "id": "<MSG_ID>", "data": { … } }
```

### macOS → Python (examples)

| `id` | `data` fields | Source |
|------|--------------|--------|
| `SIM_GPIO_OUT` | `pin`, `level`, `name` | `pal_mac_gpio` |

### Python → macOS (examples)

| `id` | `data` fields | Effect |
|------|--------------|--------|
| `SIM_BUTTON` | `pin`, `level` | Publishes `MsgButtonEvent` |

## Dependencies

- `MacDriver` — owns the TCP socket and call-backs into `ModuleSimBridge`.
- `hsys_module` — inherits from `HsysModule` so it participates in the bus
  normally (has a task, subscribes to messages).

## Location rationale

`module_sim_bridge` is a MAC-PC-only module that depends on `MacDriver`
(itself MAC-PC only). Both live under `pal/mac-pc/driver/` because they are
internal implementation details of the macOS PAL — not part of any
platform-independent interface.
