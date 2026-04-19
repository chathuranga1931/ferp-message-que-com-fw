# ModuleSpiffs

**Module ID:** `MODULE_SPIFFS_ID` (5)  
**Task:** `spiffs_task` — priority 5, 2 KB stack  
**Status:** ✅ Implemented

## Purpose

Mounts the SPIFFS filesystem at startup and broadcasts readiness. All modules
that depend on the filesystem (config, logging, retransmit) wait for
`MsgSpiffsReady` before touching any files.

## Messages

| Direction | Message | ID |
|-----------|---------|----|
| Publishes | `MsgSpiffsReady` | `0x0201` |

No subscriptions.

## Behaviour

- `pre_init()` — calls `app_spiffs_init()`. Stores result; does **not** publish yet
  (other modules have not subscribed yet at this phase).
- `post_init()` — publishes `MsgSpiffsReady` once all subscribers are registered.

## Dependencies

- `app_spiffs` (app-pheripherals) — POSIX-emulated on macOS, real SPIFFS on ESP32.
- `pal_spiffs` (PAL layer) — platform abstraction for flash filesystem.

## Simulator behaviour

On macOS, `app_spiffs_init()` maps to a POSIX directory tree rooted at
`src/product/ferp-com-simulator/SPIFFS/`. Any file read/write uses normal
`fopen`/`fread`/`fwrite` calls through the SPIFFS PAL stub.

## Notes

- Other modules must **not** call `pal_spiffs` directly — always wait for
  `MsgSpiffsReady` and then use the Storage message API (`MsgStorageRequest` —
  Sprint 10).
- If mount fails, `MsgSpiffsReady` is **not** published. Downstream modules
  should handle the absence of this message gracefully.
