# Plan: `ModuleTimeMgr` — Time Manager Module

**Status:** Planning  
**Date:** 2026-04-23

---

## 1. Objective

Implement a `ModuleTimeMgr` HSYS module that maintains accurate system time from three
cascaded sources — SPIFFS backup file, DS1307 hardware RTC, and NTP — with automatic
fallback and progressive upgrade as sources become available.

The DS1307 driver (`ds1307.hpp / ds1307.cpp`) runs **identical and unmodified** on both
the ESP32 target and the macOS simulator.  The difference is only in the I2C PAL layer
below it.

**Hardware note:** The DS1307 chip contains a small NVRAM (56 bytes, registers 0x08–0x3F)
backed by the same coin cell as the RTC oscillator.  This NVRAM is too small to store
auxiliary data reliably alongside the timekeeping registers, so SPIFFS is used for the
backup instead.

---

## 2. Source Priority and State Machine

Sources are tried in order from least to most authoritative.  Each successful source
sets the OS system time (`settimeofday`) and the module then continues trying better
sources in the background.

```
Boot
 │
 ├─[MsgSpiffsReady]──────────────────────────────────────────────────► LOAD_BACKUP
 │                                                                          │
 │                     fail (file absent / corrupt / time < 2020)           │ ok
 │                  ◄──────────────────────────────────────────────────────  │
 │                                                                          ▼
 │                                                              set sys_time = backup_time
 │                                                              publish MsgTimeStatus(BACKUP)
 │                                                                          │
 └──────────────────────────────────────────────────────────────────► LOAD_RTC
                                                                           │
                      fail (I2C error / CH bit / time < 2020)              │ ok
                   ◄──────────────────────────────────────────────────────  │
                                                                           ▼
                                                               set sys_time = rtc_time
                                                               update SPIFFS backup
                                                               publish MsgTimeStatus(RTC)
                                                                           │
                                                                           ▼
                                                                  WAIT_FOR_INTERNET
                                                                           │
                                                         [MsgInternetStatus connected=true]
                                                                           │
                                                                           ▼
                                                                     NTP_SYNC
                                                                           │
                                                               ok          │ fail (60 s timeout)
                                                         ┌─────────────────┤ retry after 60 s
                                                         │                 └──────────────────►
                                                         ▼
                                               set sys_time = ntp_time
                                               ds1307_set_time(ntp_time)       ← write RTC
                                               update SPIFFS backup
                                               publish MsgTimeStatus(NTP)
                                                         │
                                                         ▼
                                                       READY
                                               periodic backup timer (5 min)
                                               on next MsgInternetStatus → re-sync NTP
```

**Fallback on critical failure (no SPIFFS, no RTC, no internet after 20 s timeout):**
Publish `MsgTimeStatus(source=NONE, valid=false)`.  Subscribers must treat timestamps as
unreliable.

---

## 3. Files to Create / Modify

### 3.1 New files

```
src/app-modules/module_timemgr/
    module_timemgr.h            HSYS module class definition + state enum
    module_timemgr.cpp          State machine implementation

src/app-messages/
    msg_time_status.h           Broadcast: source, epoch, valid flag
    msg_time_status.cpp

src/sub-modules/pal/mac-pc/
    pal_mac_i2c.cpp             Mac I2C PAL — routes transactions to emulators by address
    pal_mac_i2c_emulator.h      Emulator callback interface
    ds1307_i2c_emulator.h       DS1307 register bank emulator interface
    ds1307_i2c_emulator.cpp     DS1307 register bank populated from gettimeofday()
```

### 3.2 Modified files

| File | Change |
|---|---|
| `src/product/app/app_hw_config.h` | NEW — I2C address for DS1307 |
| `src/product/app/app_module_ids.h` | Add `MODULE_TIMEMGR_ID = 17` |
| `src/product/app/app_msg_ids.h` | Add `MSG_ID_TIME_STATUS = 0x0204` |
| `src/product/app/app.cpp` | Add module instance + `timemgr_task` entry |
| `src/product/ferp-com-simulator/CMakeLists.txt` | Add new `.cpp` files to targets |
| `src/product/ferp-com-simulator/sim_init.cpp` | Register DS1307 emulator in pre_init |

---

## 4. Simulator I2C Architecture (Option A)

The goal is that `ds1307.cpp` calls `pal_i2c_write_read()` / `pal_i2c_write()` exactly as
it does on real hardware.  The mac PAL intercepts those calls, looks up the device address
against a table of registered emulators, and dispatches.

### 4.1 Layer diagram

```
ds1307.cpp
    │  calls pal_i2c_write_read(PORT_0, 0x68, ...)
    ▼
pal_mac_i2c.cpp                ← NEW — implements pal_i2c.h for macOS
    │  look up addr 0x68 in emulator table
    │  addr matches DS1307_I2C_ADDR (from app_hw_config.h)
    ▼
ds1307_i2c_emulator.cpp        ← NEW — in-memory DS1307 register bank
    │  on read:  populate registers from gettimeofday() → BCD encode → return bytes
    │  on write: decode BCD bytes → store as time offset
    ▼
(no real I2C bus — pure in-process function calls)
```

### 4.2 I2C address configuration

**New header: `src/product/app/app_hw_config.h`**
```c
// app_hw_config.h
// Hardware peripheral addresses and pin assignments.
// Shared by both ESP-IDF and simulator targets.
// These are compile-time hardware constants — not runtime-configurable.

#pragma once
#include <stdint.h>

// I2C device addresses
#define APP_HW_I2C_ADDR_DS1307   ((uint8_t)0x68)   // DS1307 RTC (fixed by chip spec)
```

`app_platform_pre_init()` (the simulator's override in `sim_init.cpp`) registers the
DS1307 emulator for `PORT_0, APP_HW_I2C_ADDR_DS1307` before `app_init()` starts the modules:
```cpp
pal_mac_i2c_register_device(PAL_I2C_PORT_0, APP_HW_I2C_ADDR_DS1307, &s_ds1307_emulator);
```

### 4.3 Emulator interface

```c
// pal_mac_i2c_emulator.h  (placed in src/sub-modules/pal/mac-pc/)
typedef struct {
    // Called when pal_i2c_write() targets this device
    int32_t (*on_write)(const uint8_t *data, size_t len, void *ctx);
    // Called when pal_i2c_write_read() targets this device
    int32_t (*on_write_read)(const uint8_t *wr, size_t wr_len,
                              uint8_t *rd,       size_t rd_len,  void *ctx);
    void *ctx;   // passed back to callbacks (pointer to emulator state)
} pal_i2c_emulator_t;
```

### 4.4 DS1307 emulator behaviour

`ds1307_i2c_emulator.cpp` owns an 8-byte register bank mirroring the DS1307 layout:

```
reg[0] = seconds  (BCD, bit7 = CH)
reg[1] = minutes  (BCD)
reg[2] = hours    (BCD, 24-hour)
reg[3] = day      (BCD, 1-7)
reg[4] = date     (BCD)
reg[5] = month    (BCD)
reg[6] = year     (BCD, 00-99)
reg[7] = control
```

**Read path (`on_write_read`):**
1. Receive 1-byte write (register address pointer).
2. Call `gettimeofday()` → convert to `struct tm` → BCD-encode into register bank.
3. Return `rd_len` bytes starting at the pointer register.

**Write path (`on_write`):**
1. Receive `data[0]` = register address, `data[1..n]` = values.
2. Decode BCD → `struct tm` → store a time-offset delta (macOS `settimeofday` requires root;
   applying an offset to future reads avoids that requirement while making round-trips work).

---

## 5. NTP on Simulator

On macOS the host OS is already NTP-synchronised.  The simulator NTP PAL
(`pal_esp_idf_ntp.cpp` is ESP-IDF only) needs a mac counterpart:

**`pal_mac_ntp.cpp`** (new, in `src/sub-modules/pal/mac-pc/`):
- `pal_ntp_init()` → no-op (host is synced).
- `pal_ntp_sync_start()` → no-op, immediately sets status to `PAL_NTP_SYNC_STATUS_COMPLETED`.
- `pal_ntp_get_status()` → always returns `PAL_NTP_SYNC_STATUS_COMPLETED`.
- `pal_ntp_get_epoch_time()` → calls `gettimeofday()`.

This means on the simulator, NTP "succeeds" instantly, which causes `ModuleTimeMgr` to
immediately write the host time back to the DS1307 emulator and publish `MsgTimeStatus(NTP)`.

---

## 6. SPIFFS Backup Design

### File format
Raw `time_t` value (8 bytes, little-endian) written to `/timemgr.bin`.
No JSON — avoids ArduinoJson dependency and reduces write size to minimum.

### Write guard
Write only when `|current_time − last_written_time| >= TIMEMGR_BACKUP_INTERVAL_S`
(default: 300 s, configurable via `#define`).

### Wear analysis
- Writes per day: `86400 / 300 = 288`
- Bytes per write: 8 (one SPIFFS sector = 4096 bytes; SPIFFS writes one page at a time)
- ESP32 flash endurance: ~100,000 erase cycles per sector
- SPIFFS wear levelling spreads writes across the partition
- With a 64 KB timemgr partition (separate from config): `(65536/4096) × 100,000 / 288 ≈ 5.5 years`
- Even sharing the main SPIFFS partition: the config file lives in different pages and is not
  endangered; total partition lifetime exceeds several years of continuous use.

**Config file safety:** `timemgr.bin` and `DeviceConfigs.json` are separate SPIFFS files
allocated by the SPIFFS layer in different logical pages.  A write to one does not touch
the other.

---

## 7. Messages

### `MsgTimeStatus` (0x0204) — broadcast

```cpp
struct Payload {
    time_t   epoch;          // Unix timestamp (0 = unknown)
    uint8_t  source;         // time_source_t enum
    bool     valid;          // false = no reliable source yet
};

enum time_source_t : uint8_t {
    TIME_SOURCE_NONE    = 0,
    TIME_SOURCE_BACKUP  = 1,   // SPIFFS file — least reliable
    TIME_SOURCE_RTC     = 2,   // DS1307 hardware clock
    TIME_SOURCE_NTP     = 3,   // Network Time Protocol — most reliable
};
```

Published on:
- Every source transition (BACKUP → RTC → NTP)
- Critical failure (NONE)
- Each periodic backup write (so subscribers can log the current time)

### Subscribed messages

| Message | Purpose |
|---|---|
| `MsgSpiffsReady` | Gate: start the chain when filesystem is available |
| `MsgInternetStatus` | Trigger NTP sync on connect; stop on disconnect |
| `MsgTimerAlarm` | Periodic 5-minute backup write |
| `MsgTimerStartResponse` | Confirm timer slot was allocated |

---

## 8. Module Registration

### `app_module_ids.h`
```c
#define MODULE_TIMEMGR_ID    ((hsys_module_id_t) 17)
```

### `app_msg_ids.h`
```c
MSG_ID_TIME_STATUS  = 0x0204,   ///< ModuleTimeMgr -> all: time source + epoch
```

### `app.cpp` additions
```cpp
// Module table
ModuleTimeMgr::instance(),

// Task table
{ "timemgr_task", 3072, 5, 0, { MODULE_TIMEMGR_ID, 0 } }
```

Stack budget for `timemgr_task`:
- DS1307 I2C call chain: ~256 B
- NTP PAL: ~256 B (async, no large locals)
- SPIFFS write (8-byte raw): ~512 B
- FreeRTOS Xtensa frame + logging: ~512 B
- Total used: ~1536 B → 3072 B gives comfortable 2× headroom

---

## 9. CMakeLists.txt Changes

### `app_modules` target — add source files
```cmake
${APP_MODS}/module_timemgr/module_timemgr.cpp
```

### `app_modules` target — add include directory
```cmake
${APP_MODS}/module_timemgr
${DRIVERS}/DS1307          # ds1307.hpp
${APP_HW}                  # app_hw_config.h
```

### `app_messages` target — add source file
```cmake
${APP_MSGS}/msg_time_status.cpp
```

### `pal_mac` target — add source files
```cmake
${PAL_MAC}/pal_mac_i2c.cpp
${PAL_MAC}/ds1307_i2c_emulator.cpp
${PAL_MAC}/pal_mac_ntp.cpp
```

### `DS1307` driver target (new static lib)
```cmake
add_library(drv_ds1307 STATIC ${DRIVERS}/DS1307/ds1307.cpp)
target_include_directories(drv_ds1307 PUBLIC ${DRIVERS}/DS1307 ${PAL})
target_link_libraries(app_modules PRIVATE drv_ds1307)
```

---

## 10. File Tree Summary

```
src/
├── product/app/
│   ├── app_hw_config.h            NEW — I2C address for DS1307
│   ├── app_module_ids.h           MOD — add MODULE_TIMEMGR_ID = 17
│   └── app_msg_ids.h              MOD — add MSG_ID_TIME_STATUS = 0x0204
│
├── app-modules/
│   └── module_timemgr/
│       ├── module_timemgr.h       NEW
│       └── module_timemgr.cpp     NEW
│
├── app-messages/
│   ├── msg_time_status.h          NEW
│   └── msg_time_status.cpp        NEW
│
├── sub-modules/
│   ├── drivers/DS1307/
│   │   ├── ds1307.hpp             UNCHANGED
│   │   └── ds1307.cpp             UNCHANGED
│   │
│   └── pal/
│       ├── pal_i2c.h              UNCHANGED
│       └── mac-pc/
│           ├── pal_mac_i2c.cpp           NEW — routes by address to emulators
│           ├── pal_mac_i2c_emulator.h    NEW — emulator callback interface
│           ├── ds1307_i2c_emulator.h     NEW — DS1307 emulator interface
│           ├── ds1307_i2c_emulator.cpp   NEW — register bank + gettimeofday()
│           └── pal_mac_ntp.cpp           NEW — host-time NTP stub
│
└── product/ferp-com-simulator/
    ├── CMakeLists.txt             MOD — wire new sources
    └── sim_init.cpp               MOD — register DS1307 emulator in pre_init
```

---

## 11. Open Questions (to confirm before implementation)

| # | Question | Default assumption |
|---|---|---|
| 1 | Backup interval: 5 minutes acceptable? | Yes — `TIMEMGR_BACKUP_INTERVAL_S = 300` |
| 2 | NTP server: pool.ntp.org or a specific one? | `pool.ntp.org` (same as old project) |
| 3 | Should `MsgTimeStatus` carry a formatted string (e.g. ISO-8601)? | No — just `time_t + source + valid`; callers format as needed |
| 4 | Should `timemgr_task` own the `pal_i2c_init()` call, or should that be in `app_platform_pre_init`? | `ModuleTimeMgr::pre_init()` — keeps HW init with the module that owns the HW |
| 5 | NTP retry strategy: indefinite retries or give up after N attempts? | Retry every 60 s indefinitely while internet is connected |
| 6 | `app_hw_config.h` — new file, or add I2C address to `app_config.h`? | New file — `app_config.h` is for runtime-configurable values; I2C addresses are compile-time HW constants |
