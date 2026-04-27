# OTA Module — System Requirement Specification (ToDos)
[x] The driver files which are now placed under module should be placed in the pal mac driver folder, because they are mac
    → DONE: `ota_driver_mac.c/.h` removed. `pal_mac_fw_update.cpp` (PAL layer) remains in `src/sub-modules/pal/mac-pc/` and provides the `pal_fw_update_*` backend for the simulator.
[x] Remove the ESP_PLATFORM, because application does not care the platform, and when we run the simulator, we have emulate the esp32 spiffs structure, so no need this to be specified in the app.cpp and the firmware update apis should be wrapped under mac folder to support these apis. There will be not a ota for mac, but we should have a simualtor for esp32 which implemented on mac. So the target/driver should be esp32 and that is correct, so when the otamodule improved we can add other drivers, but mac should not be there.
    → DONE: `#ifdef ESP_PLATFORM` block removed from `app.cpp`. `ota_driver_esp32_main` is now used on both platforms — the PAL layer (`pal_fw_update_*`) provides the platform-specific backend (ESP-IDF OTA partitions on device, `ota_download.bin` file on simulator).


# OTA Module — Design Specification

| Field       | Detail                |
|-------------|-----------------------|
| Document ID | FERP-SRS-OTA-001      |
| Version     | 0.4 (Draft)           |
| Date        | 2026-04-26            |
| Status      | Draft — In Discussion |

---

## 1. Overview

`ModuleOta` is the central OTA session manager. It owns:
- the session state machine (one OTA at a time),
- the source priority table,
- the target driver table,
- and the `FileSystemDriver` abstraction used by targets.

Sources (MQTT, HTTPS cloud-pull, WebServer, future BLE) request an OTA session.
`ModuleOta` grants or rejects based on the source table and current session state.
Once granted, the source receives a `const ota_fs_driver_t*` and streams binary data
directly — binary data **never passes through the message pool**. When done, the source
notifies `ModuleOta` via `MsgOtaCompleteNotify`.

---

## 2. Constraints & Assumptions

| # | Constraint |
|---|---|
| C1 | Only one OTA session may be active at a time. |
| C2 | Source and target descriptor tables are `const`, allocated at build time, passed to `OtaModule::set_tables()`. |
| C3 | Binary data does not enter the HSYS message pool. Only small control messages cross the bus. |
| C4 | Priority preemption is **not implemented** — ongoing OTA continues regardless of a new request's priority. A `priority` field is reserved in the source table as a placeholder for future implementation. |
| C5 | `timeout_ms` is an **inactivity timeout**: it resets every time `ModuleOta` receives a `MsgOtaProgress` from the active source. If no progress is received within `timeout_ms`, the session is aborted. `0` = no timeout. Sources MUST publish `MsgOtaProgress` at regular intervals to keep the session alive. |
| C6 | `ModuleOta` calls `pal_power_reset()` after publishing `MsgOtaEvent(COMPLETE)` with a short configurable delay (TBD — see open item O4). |
| C7 | On timeout or abort, `ModuleOta` calls `driver->ferase(ctx)` to clean up the partial write before returning to IDLE. |

---

## 3. Source Descriptor Table

```c
typedef struct {
    uint16_t source_module_id;  // HSYS module ID of the OTA source (hsys_module_id_t = uint16_t)
    uint8_t  priority;          // reserved — not enforced until preemption is implemented
    uint32_t timeout_ms;        // inactivity timeout in ms — resets on each MsgOtaProgress received; 0 = no timeout
} ota_source_desc_t;
```

Passed to `OtaModule::set_tables()` as `const ota_source_desc_t[]` with a count.
The module resolves a source's descriptor by matching `sender_id` from `MsgOtaStartRequest`.

---

## 4. Target Descriptor Table

```c
typedef struct {
    uint8_t                 target_idx;  // index referenced in MsgOtaStartRequest
    const char*             label;       // e.g. "esp32-main", "esp07-disptap"
    const ota_fs_driver_t*  driver;      // const pointer to this target's driver impl
    void*                   ctx;         // opaque context passed to every driver call
} ota_target_desc_t;
```

Passed to `OtaModule::set_tables()` as `const ota_target_desc_t[]` with a count.

Example targets:

| idx | label | Driver implementation |
|---|---|---|
| 0 | `esp32-main` | ESP-IDF OTA partition A/B APIs |
| 1 | `esp07-disptap` | SPIFFS file write via `pal_spiffs` |

---

## 5. FileSystemDriver (Middleware)

**Location:** `src/sub-modules/middleware/FileSystemDriver.h` + `FileSystemDriver.cpp`

OTA-specific binary streaming abstraction. Its "reusability" is within the OTA domain only —
swapping the binary write backend (e.g. ESP-IDF OTA partition A/B vs. SPIFFS file) without
changing `OtaModule`. It is **not** the general filesystem middleware; `ModuleRetransmit` and
other modules use `storage_interface_t` from the list-manager middleware instead.

```c
typedef enum {
    OTA_FS_OK              =   0,
    OTA_FS_ERR_NOT_OPEN    =  -1,
    OTA_FS_ERR_WRITE_FAIL  =  -2,
    OTA_FS_ERR_READ_FAIL   =  -3,
    OTA_FS_ERR_ERASE_FAIL  =  -4,
    OTA_FS_ERR_INVALID_ARG =  -5,
    OTA_FS_ERR_NO_SPACE    =  -6,
    OTA_FS_ERR_TIMEOUT     =  -7,
    OTA_FS_ERR_UNKNOWN     = -99,
} ota_fs_err_t;

typedef enum {
    OTA_FS_OPEN_WRITE  = 0,
    OTA_FS_OPEN_APPEND = 1,
    OTA_FS_OPEN_READ   = 2,
} ota_fs_open_mode_t;

typedef struct {
    ota_fs_err_t (*fopen) (void* ctx, const char* path, ota_fs_open_mode_t mode);
    ota_fs_err_t (*fclose)(void* ctx);
    ota_fs_err_t (*fwrite) (void* ctx, const uint8_t* data, uint32_t len);
    ota_fs_err_t (*fappend)(void* ctx, const uint8_t* data, uint32_t len);
    ota_fs_err_t (*fread)  (void* ctx, uint8_t* buf, uint32_t len, uint32_t* out_len);
    ota_fs_err_t (*ferase) (void* ctx);
} ota_fs_driver_t;
```

`ctx` is the opaque `void*` from the target descriptor. The driver implementation uses it
to hold a partition handle, file descriptor, or SPIFFS path — whatever is appropriate per target.

---

## 6. OTA Session State Machine

```
         ┌─────────┐
         │  IDLE   │◄──────────────────────────────────────────────┐
         └────┬────┘                                               │
              │ MsgOtaStartRequest (accepted)                      │
              ▼                                                     │
       ┌─────────────┐                                             │
       │   PENDING   │  waiting for MsgOtaRequestDriver            │
       └──────┬──────┘                                             │
              │ MsgOtaRequestDriver received                       │
              │ → reply MsgOtaDriverResponse                       │
              ▼                                                     │
        ┌──────────┐   MsgOtaAbortRequest or timeout               │
        │  ACTIVE  │──────────────────────────────────────────┐    │
        └─────┬────┘                                          │    │
              │                                               ▼    │
              │ MsgOtaCompleteNotify           ┌───────────────────┐│
              │                               │     ABORTING      ││
              ▼                               │  ferase, notify   ││
       ┌─────────────┐                        │  source           │┘
       │  COMPLETE   │                        └───────────────────┘
       │ MsgOtaEvent │
       │ pal_reset() │
       └─────────────┘
```

| State | Entry action | Exit trigger |
|---|---|---|
| IDLE | — | `MsgOtaStartRequest` from a known source |
| PENDING | Start fixed deadline timer (`timeout_ms` from session start) | `MsgOtaRequestDriver` received → ACTIVE, or deadline fires → ABORTING |
| ACTIVE | Reply with driver pointer; arm inactivity timer | `MsgOtaCompleteNotify` → COMPLETE; `MsgOtaProgress` received → **reset inactivity timer**; inactivity timer fires or `MsgOtaAbortRequest` → ABORTING |
| ABORTING | `ferase(ctx)`, publish `MsgOtaEvent(SESSION_ABORTED)`, notify source | always → IDLE |
| COMPLETE | Publish `MsgOtaEvent(COMPLETE)`, wait delay, `pal_power_reset()` | → IDLE |

---

## 7. Message Definitions

All messages are in the `0x0Axx` range. All are **DIRECT** except `MsgOtaEvent` and `MsgOtaProgress`.

| ID | Message | Type | Direction | Key Payload |
|---|---|---|---|---|
| 0x0A01 | `MsgOtaStartRequest` | DIRECT | Source → OtaModule | `uint8_t target_idx`, `char incoming_version[32]` |
| 0x0A02 | `MsgOtaStartResponse` | DIRECT | OtaModule → Source | `ota_start_result_t result` |
| 0x0A03 | `MsgOtaRequestDriver` | DIRECT | Source → OtaModule | no payload — `sender_id` identifies the session |
| 0x0A04 | `MsgOtaDriverResponse` | DIRECT | OtaModule → Source | `const ota_fs_driver_t* driver`, `void* ctx` |
| 0x0A05 | `MsgOtaAbortRequest` | DIRECT | Source → OtaModule | `ota_abort_reason_t reason` |
| 0x0A06 | `MsgOtaCompleteNotify` | DIRECT | Source → OtaModule | `bool success`, `ota_fs_err_t last_error` |
| 0x0A07 | `MsgOtaEvent` | NOTIFICATION | OtaModule → subscribers | `ota_event_id_t event`, `uint8_t target_idx`, `char version[32]` |
| 0x0A08 | `MsgOtaProgress` | NOTIFICATION | Source → subscribers | `uint8_t target_idx`, `uint8_t percent`, `uint32_t bytes_written`, `uint32_t total_bytes` |

### `ota_start_result_t` values

| Value | Meaning |
|---|---|
| `OTA_START_ACCEPTED` | Session granted; source should send `MsgOtaRequestDriver` next |
| `OTA_START_REJECTED_BUSY` | Another OTA is already active |
| `OTA_START_REJECTED_UNKNOWN_SOURCE` | `sender_id` not found in source table |
| `OTA_START_REJECTED_UNKNOWN_TARGET` | `target_idx` not found in target table |

### `ota_abort_reason_t` values

| Value | Meaning |
|---|---|
| `OTA_ABORT_SOURCE_CANCELLED` | Source voluntarily cancelled |
| `OTA_ABORT_WRITE_ERROR` | Driver write failure |
| `OTA_ABORT_SOURCE_DISCONNECTED` | Underlying transport lost |

### `ota_event_id_t` values

| Value | Description |
|---|---|
| `OTA_EVENT_SESSION_STARTED` | Session granted and driver handed to source |
| `OTA_EVENT_SESSION_ABORTED` | Session cancelled (abort or timeout) |
| `OTA_EVENT_COMPLETE` | Binary written successfully; reboot imminent |
| `OTA_EVENT_TIMEOUT` | Session timed out |

Subscribers of `MsgOtaEvent`: `ModuleLeds`, `ModuleBuzzer`, `ModuleCloud`.

### `MsgOtaProgress` detail

Published by the **OTA source** (not `ModuleOta`) during the ACTIVE state, directly to all
subscribers. **`ModuleOta` also subscribes to `MsgOtaProgress`** solely to reset its
inactivity timer — it does not otherwise process or forward the message.

```c
typedef struct {
    uint8_t  target_idx;      // same index as used in MsgOtaStartRequest
    uint8_t  percent;         // 0–100
    uint32_t bytes_written;   // cumulative bytes written so far
    uint32_t total_bytes;     // total firmware size (0 if unknown)
} ota_progress_payload_t;
```

**Throttle:** The source decides how often to publish. Common strategies:
- Every N% change (e.g. every 10% — only publish when `percent / 10` changes)
- Every N bytes (e.g. every 64 KB)
- Both combined (whichever fires first)

The granularity is a source implementation detail, but the source **must publish at least
once per `timeout_ms / 2`** to keep the inactivity timer from firing. If `timeout_ms = 0`
the source is exempt from this requirement.

Sources that do not know the total size set `total_bytes = 0`; consumers should treat
`percent` as the authoritative progress indicator in that case.

Subscribers of `MsgOtaProgress`: `ModuleOta` (inactivity timer reset), `ModuleLeds`, `ModuleBuzzer`.

---

## 8. Message Flow

### Happy path

```
Source                   OtaModule
  │                          │
  │── MsgOtaStartRequest ───►│  (target_idx, incoming_version)
  │                          │  lookup sender in source table
  │◄── MsgOtaStartResponse ──│  result = ACCEPTED
  │                          │
  │── MsgOtaRequestDriver ──►│
  │◄── MsgOtaDriverResponse ─│  (driver*, ctx)
  │                          │
  │  [source: fopen → fwrite × N → fclose]
  │  [source publishes MsgOtaProgress(0%), …(50%), …(100%) directly]
  │                          │
  │── MsgOtaCompleteNotify ─►│  success = true
  │                          │  publish MsgOtaEvent(COMPLETE)
  │                          │  delay → pal_power_reset()
```

### Abort / error path

```
Source                   OtaModule
  │── MsgOtaStartRequest ───►│
  │◄── MsgOtaStartResponse ──│  ACCEPTED
  │── MsgOtaRequestDriver ──►│
  │◄── MsgOtaDriverResponse ─│
  │                          │
  │  [write error occurs]    │
  │── MsgOtaAbortRequest ───►│  reason = WRITE_ERROR
  │                          │  driver->ferase(ctx)
  │                          │  publish MsgOtaEvent(SESSION_ABORTED)
  │                          │  → IDLE
```

### Busy rejection

```
Source B                 OtaModule               Source A (active)
  │── MsgOtaStartRequest ───►│
  │                          │  session active for Source A
  │◄── MsgOtaStartResponse ──│  result = REJECTED_BUSY
  │  (Source B waits or      │
  │   retries per its own    │
  │   implementation)        │
```

---

## 9. Module Init

```c
void OtaModule::set_tables(
    const ota_source_desc_t* sources, uint8_t source_count,
    const ota_target_desc_t* targets, uint8_t target_count
);
```

Called before `app_init()` — identical pattern to `ModuleCloud::set_driver()`.

---

## 10. File Structure

See Section 11.3 for the complete, integration-aware file structure.

---

## 11. Integration Guide

This section describes how `ModuleOta` is wired into the two concrete firmware targets.

---

### 11.1 ESP32 Main Application (`esp32-main`, target idx 0)

#### Partition Table

The current single-slot `factory` layout **must be replaced** with a dual-slot OTA layout
before `ModuleOta` can be used on the ESP32 main application.

Proposed `partitions.csv`:

```
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   24K
phy_init, data, phy,      0xF000,   4K
otadata,  data, ota,      0x10000,  8K
ota_0,    app,  ota_0,    0x20000,  960K
ota_1,    app,  ota_1,    0x120000, 960K
spiffs,   data, spiffs,   0x220000, 128K
```

> Flash is 4 MB. The two OTA slots (`ota_0`, `ota_1`) are each 960 KB — matching the
> current factory slot size. SPIFFS is reduced to 128 KB; expand if needed.

#### Driver Implementation

File: `src/app-modules/module_ota/targets/ota_driver_esp32_main.c`

The driver wraps the ESP-IDF OTA partition APIs:

| `ota_fs_driver_t` call | ESP-IDF call | Notes |
|---|---|---|
| `fopen(ctx, path, WRITE)` | `esp_ota_begin(next_partition, OTA_SIZE_UNKNOWN, &handle)` | `path` ignored — next OTA slot is selected automatically; `handle` stored in `ctx` |
| `fwrite(ctx, data, len)` | `esp_ota_write(handle, data, len)` | Streams binary chunk |
| `fappend(ctx, data, len)` | `esp_ota_write(handle, data, len)` | Same as `fwrite` for this target |
| `fclose(ctx)` | `esp_ota_end(handle)` then `esp_ota_set_boot_partition(next_partition)` | Validates image and sets boot slot |
| `ferase(ctx)` | `esp_ota_abort(handle)` | Called by `ModuleOta` on abort/timeout to release the partition handle |
| `fread` | not used | Returns `OTA_FS_ERR_INVALID_ARG` |

The `ctx` passed in `ota_target_desc_t` for this target holds a small struct:

```c
typedef struct {
    esp_ota_handle_t    handle;
    const esp_partition_t* next_partition;
} ota_esp32_ctx_t;
```

`next_partition` is resolved once inside `fopen` via `esp_ota_get_next_update_partition(NULL)`.

#### Post-OTA Reboot

After `fclose` succeeds, `ModuleOta` receives `MsgOtaCompleteNotify(success=true)`,
publishes `MsgOtaEvent(OTA_EVENT_COMPLETE)`, waits the configured delay, then calls
`pal_power_reset()`. The ESP32 reboots into the new firmware slot.

#### OTA Source: `ModuleMqtt`

`ModuleMqtt` is the primary OTA trigger source for the ESP32 main application:

1. `ModuleMqtt` subscribes to a device-specific MQTT topic (e.g. `ferp/{mac}/ota/start`).
2. On receiving an OTA command it publishes **`MsgOtaStartRequest`** (target_idx=0, version).
3. `ModuleOta` replies **`MsgOtaStartResponse(ACCEPTED)`**.
4. `ModuleMqtt` requests the driver (`MsgOtaRequestDriver`), receives the `ota_fs_driver_t*`.
5. `ModuleMqtt` opens an HTTPS connection to the firmware URL (from the MQTT payload), reads
   chunks, and calls `driver->fwrite(ctx, chunk, len)` directly — binary never enters the pool.
6. `ModuleMqtt` publishes `MsgOtaProgress` after every chunk (throttled to avoid pool pressure).
7. On completion or error, `ModuleMqtt` sends `MsgOtaCompleteNotify`.

#### `set_tables()` Wiring — both platforms

Wiring is in `src/product/app/app.cpp` via `ota_platform_get_config()` — called by
`OtaModule` during init. Because `ota_driver_esp32_main` delegates all I/O to
`pal_fw_update_*`, the same driver struct is used on device and simulator:

```cpp
// In src/product/app/app.cpp (shared — no #ifdef required)
#include "ota_driver_esp32_main.h"

static ota_esp32_ctx_t s_esp32_ota_ctx = {};

static const ota_source_desc_t k_ota_sources[] = {
    { MODULE_MQTT_ID, 0, 0, 60000 },  // 60 s inactivity timeout
};

static const ota_target_desc_t k_ota_targets[] = {
    { .target_idx   = 0,
      .needs_reboot = true,
      .label        = "esp32-main",
      .driver       = &g_ota_driver_esp32_main,
      .ctx          = &s_esp32_ota_ctx },
};

extern "C" void ota_platform_get_config(
    const ota_source_desc_t **sources, uint8_t *source_count,
    const ota_target_desc_t **targets, uint8_t *target_count)
{
    *sources      = k_ota_sources;
    *source_count = ARRAY_SIZE(k_ota_sources);
    *targets      = k_ota_targets;
    *target_count = ARRAY_SIZE(k_ota_targets);
}
```

On the simulator `pal_mac_fw_update.cpp` provides the `pal_fw_update_*` backend that
streams the binary to `<cwd>/ota_download.bin`. No reboot is performed (the simulator
stub's `pal_power_reset()` calls `std::exit(0)`).

---

### 11.2 DispaTap Co-Processor (`esp07-disptap`, target idx 1)

#### Concept

The ESP07 (DispaTap co-processor) does **not** share the ESP32 flash. Its firmware is
delivered as a binary file staged on the ESP32's SPIFFS, then streamed to the ESP07 over
UART by `ModuleDispTap` after OTA completes. The ESP32 itself does **not** reboot for
this target.

#### Driver Implementation

File: `src/app-modules/module_ota/targets/ota_driver_esp07_disptap.c`

The driver wraps `pal_spiffs` file I/O, writing to a fixed staging path
(`/ota_esp07.bin`):

| `ota_fs_driver_t` call | Action |
|---|---|
| `fopen(ctx, path, WRITE)` | `pal_spiffs_open("/ota_esp07.bin", "wb")` — `path` from request is ignored; staging path is fixed |
| `fwrite(ctx, data, len)` | `pal_spiffs_write(fd, data, len)` |
| `fappend(ctx, data, len)` | `pal_spiffs_write(fd, data, len)` |
| `fclose(ctx)` | `pal_spiffs_close(fd)` — file is now ready for UART transfer |
| `ferase(ctx)` | Close fd (if open) and `pal_spiffs_remove("/ota_esp07.bin")` |
| `fread` | not used |

The `ctx` for this target holds the SPIFFS file descriptor:

```c
typedef struct {
    int fd;   // pal_spiffs file handle; -1 = not open
} ota_esp07_ctx_t;
```

#### Post-OTA Handoff — No ESP32 Reboot

When `ModuleOta` receives `MsgOtaCompleteNotify(success=true)` for target idx 1:

1. `ModuleOta` publishes `MsgOtaEvent(OTA_EVENT_COMPLETE, target_idx=1)`.
2. **`ModuleOta` does NOT call `pal_power_reset()`** for this target (see Open Item O3 — resolved via `needs_reboot` flag in `ota_target_desc_t`).
3. `ModuleDispTap` (or a dedicated `ModuleDispTapOta` task) **subscribes to `MsgOtaEvent`** and, on `COMPLETE` with `target_idx == 1`, initiates the UART transfer sequence:
   - Assert ESP07 reset (GPIO low)
   - Open `/ota_esp07.bin` from SPIFFS
   - Stream binary using the ESP07 bootloader SLIP protocol over UART
   - Assert ESP07 reset release; verify ESP07 is running new firmware
   - Delete `/ota_esp07.bin` from SPIFFS

> The UART transfer is opaque to `ModuleOta` — OTA is complete from `ModuleOta`'s
> perspective once the binary is staged on SPIFFS.

#### OTA Source: `ModuleMqtt`

Same flow as for the ESP32 main target but with `target_idx = 1` in `MsgOtaStartRequest`.
A separate MQTT topic (e.g. `ferp/{mac}/ota/disptap`) carries the trigger and firmware URL.
`ModuleMqtt` downloads the ESP07 binary and streams it through the SPIFFS driver identically
to the esp32-main flow.

#### `set_tables()` Wiring — both targets combined

In practice both targets are registered in the same call. The `set_tables()` call in
`main.cpp` expands to:

```cpp
static ota_esp07_ctx_t  s_esp07_ota_ctx = { .fd = -1 };
static const ota_fs_driver_t s_esp07_ota_driver = {
    .fopen   = ota_esp07_fopen,
    .fclose  = ota_esp07_fclose,
    .fwrite  = ota_esp07_fwrite,
    .fappend = ota_esp07_fwrite,
    .fread   = ota_esp07_fread_stub,
    .ferase  = ota_esp07_ferase,
};

static const ota_target_desc_t s_ota_targets[] = {
    { .target_idx = 0, .label = "esp32-main",    .driver = &s_esp32_ota_driver, .ctx = &s_esp32_ota_ctx },
    { .target_idx = 1, .label = "esp07-disptap", .driver = &s_esp07_ota_driver, .ctx = &s_esp07_ota_ctx },
};
```

---

### 11.3 File Structure

| File | Location | Notes |
|---|---|---|
| `OtaModule.h` / `.cpp` | `src/app-modules/module_ota/` | Core module |
| `FileSystemDriver.h` | `src/sub-modules/middleware/` | `ota_fs_driver_t` interface |
| `ota_driver_esp32_main.c/.h` | `src/app-modules/module_ota/targets/` | ESP-IDF OTA partition driver — used on device **and** simulator |
| `ota_driver_esp07_disptap.c` | `src/app-modules/module_ota/targets/` | SPIFFS staging driver (future) |
| `pal_mac_fw_update.cpp` | `src/sub-modules/pal/mac-pc/` | Simulator PAL backend — implements `pal_fw_update_*`; streams binary to `<cwd>/ota_download.bin` |
| `partitions.csv` | `src/product/ferp-com-esp32-idf/` | **Must be updated** — add OTA A/B slots |

> **Design principle**: `module_ota/targets/` contains only firmware-target drivers (one per
> flashable device). Platform/simulator adaptation is the PAL's responsibility
> (`src/sub-modules/pal/`). There is no mac OTA target — mac is a simulator, not a firmware target.

---

## 12. Open Items

| # | Item |
|---|---|
| O1 | **Priority preemption** — `priority` field reserved in source table. Behaviour undefined; placeholder only. |
| O2 | ~~**PENDING timeout**~~ — **Resolved.** PENDING uses `timeout_ms` as a fixed deadline (no writes happen there, so no inactivity reset is possible). Same value reused. |
| O3 | **Reboot for co-processor target** — for `esp07-disptap` (SPIFFS file, no CPU reset needed), should `needs_reboot` be a field in `ota_target_desc_t`, or carried in `MsgOtaCompleteNotify`? |
| O4 | **Reset delay** — time between `MsgOtaEvent(COMPLETE)` and `pal_power_reset()`: fixed constant in `user_config.h` or a field in `ota_target_desc_t`? |

---

## Revision History

| Version | Date | Description |
|---|---|---|
| 0.1 | 2026-04-26 | Initial draft based on design discussion |
| 0.2 | 2026-04-26 | Fix `source_module_id` to `uint16_t`; add `MsgOtaProgress` (0x0A08) |
| 0.3 | 2026-04-26 | Change `timeout_ms` to inactivity timeout (resets on `MsgOtaProgress`); PENDING uses fixed deadline; resolve O2 |
| 0.4 | 2026-04-26 | Add Section 11 — Integration Guide: ESP32 main (ESP-IDF OTA A/B) and DispaTap (SPIFFS staging + UART handoff) |
| 0.5 | 2026-04-27 | Remove `ota_driver_mac` — mac is not an OTA target; `ota_driver_esp32_main` used on both platforms via PAL; remove `#ifdef ESP_PLATFORM` from `app.cpp`; update file structure and wiring sections |