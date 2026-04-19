# ModuleTimer

**Module ID:** `MODULE_TIMER_ID` (7)  
**Task:** `timer_task` — priority 4, 2 KB stack  
**Status:** ✅ Implemented

## Purpose

Shared software timer service. Any module can request a one-shot or repetitive
timer without creating its own OS timer. Timers are identified by the
requesting module's ID and delivered back via DIRECT messages.

## Messages

| Direction | Message | ID | Notes |
|-----------|---------|----|-------|
| Subscribes | `MsgTimerStart`         | `0x0100` | Request a new timer slot |
| Subscribes | `MsgTimerStop`          | `0x0101` | Cancel a running timer |
| Publishes  | `MsgTimerStartResponse` | `0x0102` | DIRECT — result to requester |
| Publishes  | `MsgTimerStopResponse`  | `0x0103` | DIRECT — result to requester |
| Publishes  | `MsgTimerAlarm`         | `0x0104` | DIRECT — alarm to registered module |

## Usage example

```cpp
// Start a 250 ms repetitive timer
MsgTimerStart::Payload p{};
p.source_module_id = id();
p.duration_ms      = 250;
p.is_repetitive    = true;
publish(MsgTimerStart::create(id(), p));

// Handle the alarm
case MsgTimerAlarm::ID: {
    auto p = MsgTimerAlarm::deserialize(msg);
    if (p.source_module_id == id()) { /* do work */ }
    break;
}
```

## Behaviour

- Up to 20 simultaneous timer slots.
- Resolution: 100 ms (one `hsys_soft_timer` tick per slot).
- Each slot is identified by `source_module_id` — one active timer per module.
- Repetitive timers repeat until `MsgTimerStop` is received.
- `MsgTimerAlarm` and responses are DIRECT — delivered only to the requesting
  module's task queue.

## Notes

- `ModuleLeds` does **not** use `ModuleTimer` — it uses `hsys_led` which drives
  `hsys_soft_timer` directly (sub-100 ms resolution needed for LED patterns).
- For coarse periodic work (seconds), subscribe to `MsgTick1000ms` from
  `Ticker` instead of starting a 1000 ms timer here.
