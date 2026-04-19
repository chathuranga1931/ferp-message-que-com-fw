# ModuleSysmon

**Module ID:** `SYSMON_MODULE_ID` (4)  
**Task:** `sysmon_task` — priority 3, 2 KB stack  
**Status:** ✅ Implemented

## Purpose

System monitor. Periodically logs task stack high-watermarks, heap free space,
and pool utilisation to help detect memory leaks and stack overflows during
development.

## Messages

| Direction | Message | ID | Notes |
|-----------|---------|----|-------|
| Subscribes | `MsgTick1000ms` | `0x0200` | Triggers periodic report |

No outgoing messages — output is via `pal_logger` only.

## Behaviour

- On every `MsgTick1000ms`: samples heap and pool usage, logs a compact report.
- Runs at the lowest task priority (3) so it never starves domain modules.

## Dependencies

- `hsys_pool` — pool usage counters.
- `pal_logger` — log output only.

## Notes

- Simulator: heap/stack numbers reflect macOS process memory (not ESP32 SRAM).
  The values are useful for relative trends but not absolute ESP32 sizing.
- Final pool sizing for production should be done from sysmon reports captured
  during a full hardware integration test run.
