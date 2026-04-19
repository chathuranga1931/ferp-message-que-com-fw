# Ticker

**Module ID:** `TICKER_MODULE_ID`  
**Task:** `ticker_task` — priority 6, 1 KB stack  
**Status:** ✅ Implemented

## Purpose

Generates a 1-second heartbeat broadcast. Every other time-dependent module
derives its cadence from this tick rather than creating its own timer.

## Messages

| Direction | Message | ID |
|-----------|---------|-----|
| Publishes | `MsgTick1000ms` | `0x0200` |

No subscriptions.

## Behaviour

- `init()` — starts an internal `hsys_soft_timer` at 1000 ms auto-reload.
- On each tick: publishes `MsgTick1000ms` (no payload).

## Dependencies

- `hsys_soft_timer` (OS layer) — the only timer the Ticker creates directly.
- No PAL dependencies.

## Notes

- The tick is not a wall-clock time source. Use `ModuleTime` for real timestamps.
- Any module that needs a different period should use `ModuleTimer` (request
  via `MsgTimerStart`) rather than subscribing to `MsgTick1000ms` and counting.
