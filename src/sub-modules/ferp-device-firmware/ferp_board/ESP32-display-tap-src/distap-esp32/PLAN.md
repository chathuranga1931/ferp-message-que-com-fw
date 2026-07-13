# Plan: Raw-capture test firmware for unknown pumps (types 90+)

## 0. Implementation status

**Build-verified** — an ESP-IDF v5.5.3 install and a PlatformIO install were
both located on this machine (paths differed from what `flash.sh` assumed,
see the fixes below) and used to actually build every relevant target:

| Target | Result |
|---|---|
| `distap-esp32` default build (`idf.py build`) | ✅ builds clean |
| `distap-esp32` raw-capture build (`sdkconfig.raw_capture.defaults`, `CONFIG_DISTAP_RAW_CAPTURE_ONLY=y`) | ✅ builds clean, binary shrinks (`text` 215429→185721 B, `bss` 6641→3353 B), `PROJECT_VER` correctly baked in as `99.0.0.1` vs `1.0.0.7` |
| `ESP32-display-tap-src/main-esp32` (bench harness, PlatformIO `esp32-board-2602`) | ✅ builds clean |
| `ferp-device-firmware/.../main-esp32` (production, PlatformIO `esp32-board-2602`) | ✅ builds clean |
| `ferp-com-simulator` (CMake, exercises `fuel_disptap_driver.cpp`/`module_fuel.cpp`/`mac_com_distap.cpp`) | ✅ builds clean |
| `ferp-com-esp32-idf` (product ESP-IDF build) | ❌ fails at CMake *configure* (`esp_hal_wdt` component unresolved) — fails before any source file compiles, unrelated to this feature, not chased further |

Bugs found and fixed **while verifying** (three categories):

**A. Real bugs in this feature's own new code:**
- `-Werror=switch`: `device_init()`'s `switch (settings.display)` rejected
  `case DIS_RAW_8BIT_V1:`/`91` because they were plain `#define`s, not
  members of the `display_type_t` enum being switched on. Fixed by making
  them real enumerators with explicit values (`DIS_RAW_8BIT_V1 = 90,
  DIS_RAW_12BIT_V1 = 91,`) listed *after* `DIS_SIZE` in all three
  `device.h`/`display_types.h` copies, so `DIS_SIZE` itself is untouched
  (stays 8) while the switch compiles clean. `#define DIS_RAW_TYPE_BASE 90`
  stays a plain macro since `is_raw_capture_type()` only needs the number.
- `raw_capture_chunk_t.total_len` was originally sketched as `uint8_t` in
  an earlier revision of this plan (200-byte batches); when the batch size
  moved to 256 the field had to become `uint16_t` (256 doesn't fit in a
  byte) — caught and fixed before implementation, see §4.2.

**B. Pre-existing bugs, unrelated to this feature, that blocked verifying
it (fixed so the actual code changes could be build-tested):**
- `flash.sh` / `flash_raw_capture.sh`: `IDF_PATH="/Volumes/Data/esp/..."` —
  `/Volumes/Data` is a stale symlink to `/` on this machine; real install is
  `/Users/chathuranga/DATA/esp/v5.5.3/esp-idf`. Fixed both scripts.
- `extra_script.py` in **both** `main-esp32` PlatformIO trees: line 1 was
  `.# Import("env")` — a stray leading `.` makes it a Python syntax error,
  which PlatformIO evaluates unconditionally on every build (even though
  the line is meant to be dead/commented-out code, superseded by the real
  `Import("env")` further down). Removed the stray `.` in both copies.
- `board_inf.h` vs `board.h`: the production tree (`ferp-device-firmware/
  .../main-esp32`) renamed its board-pinout umbrella header from
  `board_inf.h` to `board.h` at some point, but four files still referenced
  the old name (`lib/device/device.h`, `src/main.cpp`,
  `lib/production_test/production_test.cpp`, and all three
  `lib/device/board_23xx.h` variants) — confirmed via `diff` these files
  are otherwise byte-identical to their working bench-harness twins (which
  correctly reference `board_inf.h`, the name still used there). Updated
  all five `#include`s to `"board.h"` to match this tree's actual filename.

**C. Confirmed non-issue:** `components/device/CMakeLists.txt`'s
conditional `PRIV_REQUIRES` does **not** stop ESP-IDF from *compiling* the
excluded pump components (censtar/hongyang/longfeng/sanki/wayn) in the
raw-capture build — ESP-IDF auto-discovers every folder under
`components/` as a candidate component regardless of any component's
`PRIV_REQUIRES` list, so those `.c` files still get compiled into static
libraries. What actually delivers the "smaller test firmware" goal is
`-ffunction-sections -fdata-sections` + the default `--gc-sections` link
step: since nothing in the raw-capture build calls
`display_sanki_6_digit_init()` etc. anymore, the linker drops them.
Verified directly with `nm` on both `.elf` files — `display_sanki_6_digit_init`
/`display_hongyang_8_digit_init`/`display_censtar_6_digit_init` are present
in the default build's ELF and **completely absent** from the raw-capture
build's ELF, while `display_raw_capture_init` is present only in the
latter. The flash-size goal is genuinely met; it just happens via linker
dead-code elimination rather than skipped compilation (build time isn't
improved, binary size is).

Implemented (code, all six items below build-verified above):

- §4 wire protocol (new packet IDs, `raw_capture_chunk_t`, `is_raw_capture_type()`,
  both validation fixes) — landed in distap-esp32's `device.h`/`device.c` and
  in **all four** copies of the main-side protocol code (see the "fourth and
  fifth copy" note below).
- §5 DT board: `components/raw_capture/` (generic 8/12-bit capture + 256B/
  4×64B/2s chunked send per data line), wired into `device_init()`. One
  deviation from the original §5.1 sketch: implemented as a single
  `display_raw_capture_init(send_queue, codeword_bits)` call that sets up
  *both* channels internally (matching how every existing pump driver in
  this codebase — `sanki_6_digit.c` etc. — is structured), not "one instance
  per channel" with a `raw_pck_id` parameter. `RCLK` is treated as the
  standard shift-register latch signal (one completed codeword per `RCLK`
  edge, whatever bit count accumulated since the last edge — no filtering
  against the nominal `codeword_bits`, since capturing the pump's actual
  behaviour, mismatched or not, is the point of this driver).
- **Both `SDATA1` and `SDATA2` are captured, as fully independent streams,
  each under its own command (pck_id) — not a shared ID plus a "which
  line" field.** SDATA1 and SDATA2 share one SCLK/RCLK per physical
  channel but carry independent pump data, so on every `SCLK` edge both
  lines are shifted into *separate* accumulators (`accumulator_dis_1_l1`/
  `_l2` etc.), and on `RCLK` both are latched into *separate* buffers —
  sharing one `buf_idx` per channel since both buffers always fill in
  exact lockstep (same clock, same latch, same byte count per event).
  **Two design iterations happened here, in order:**
  1. First cut: one pck_id per channel (`TX_ID_RAW_DIS1_DATA`/`_DIS2_DATA`)
     plus a `data_line` (1/2) field in `raw_capture_chunk_t` to tell the
     two lines apart, at the original 256-byte batch size — 8 packets per
     2s cycle per channel, ~1024B combined across DIS1+DIS2 per 2s.
  2. Revised to **4 distinct pck_ids** — one per channel *and* line
     (`TX_ID_RAW_DIS1_L1_DATA` / `_L2` / `TX_ID_RAW_DIS2_L1_DATA` / `_L2`,
     mirrored as `RX_ID_RAW_*` on main) — so each of the four streams is
     fully identified by its own command, no shared field needed;
     `data_line` was dropped from `raw_capture_chunk_t` entirely. At the
     same time the batch size was **halved to 128 bytes** (`RAW_CHUNK_LEN`
     32) so total bandwidth per pacing cycle stays where it was before
     going to 4 streams: still 8 packets per 2s cycle per channel, but
     each packet now carries half the payload — ~512B combined across
     DIS1+DIS2 per 2s, matching the original single-line-era budget.
  All log consumers (production `fuel_disptap_driver.cpp`, both
  bench-harness/production `device.cpp` `Serial` dumps, and the
  simulator's `mac_distap_inject_raw_chunk()`) were updated to register 4
  raw callbacks (one per channel+line) instead of 2, and
  `init_comms_distap()`'s signature grew from 2 raw callback parameters to
  4 across all copies. Rebuilt and reverified across all five buildable
  targets after both changes (§0 table).
- §5.3 Kconfig/CMake build split (`CONFIG_DISTAP_RAW_CAPTURE_ONLY`,
  `sdkconfig.raw_capture.defaults`, `flash_raw_capture.sh`).
- §6 main-side callback plumbing and both consumers (production
  `fuel_disptap_driver.cpp` logging, bench-harness `device.cpp` Serial dump).

**Found while implementing — two more copies of this protocol code than
§1 originally accounted for:**
1. `src/sub-modules/pal/mac-pc/driver/mac_com_distap.cpp` /
   `mac_cmd_distap.cpp` — the **simulator** implementation of
   `com_distap.h`/`cmd_distap.h`, linked in unconditionally by
   `fuel_disptap_driver.cpp` (no `#ifdef`, per that file's own header
   comment). Changing `init_comms_distap()`'s signature broke this build
   too; fixed it and added a symmetric `mac_distap_inject_raw_chunk()` next
   to the existing `mac_distap_inject_frame()`, so the raw-logging path is
   actually testable through the simulator without real DT hardware — the
   only way to exercise this end-to-end in an environment with no ESP-IDF
   toolchain.
2. `src/sub-modules/ferp-device-firmware/ferp_board/main-esp32/lib/device/device.cpp`
   itself (not just `com_distap.c`/`.h`/`display_types.h`) turned out to be
   **byte-identical** to the bench-harness's `device.cpp` — same dead
   `fuel_event_display_1/2` stubs, same `init_comms_distap()` call from its
   own `initBoard()` (called from that tree's own `src/main.cpp`). Fixed in
   lockstep with the bench-harness copy so both stayed identical (verified
   with `diff` after editing).

So the real count is **five** files needing the `init_comms_distap()`
signature change, not the three headers §1 originally called out — all
five are now consistent (verified via `grep` that no 2-argument call/decl
sites remain anywhere in the repo outside `documents/HSYS FW User Manual.md`,
which is a conceptual architecture diagram, not literal API reference, and
was left as-is).

## 1. What this project is (as-built)

`distap-esp32` ("**Display Tap**", DT board) sits physically between a fuel
pump's digital display driver signals (SCLK / RCLK / SDATA1 / SDATA2 / CS ×2
channels, see `components/device/include/board_io.h`) and the "**main**"
ESP32 (`ferp-com-main`, code lives in
`src/sub-modules/ferp-device-firmware/ferp_board/main-esp32`).

- DT board captures the GPIO bit-stream for a specific pump protocol
  (censtar/hongyang/longfeng/sanki/wayn — one component per pump under
  `components/`), decodes it into a fixed-size, pump-agnostic struct
  `display_data_t`, and sends it to main over UART0 framed as `data_packet_t`
  with `pck_id = TX_ID_DIS1_DATA`/`TX_ID_DIS2_DATA` and `display = <pump
  type>`.
- Main (`com_distap.c` + `module_fuel.cpp`) receives that, and for known
  types runs it through `pump_drivers[DIS_SIZE]` (`module_fuel.cpp:354`)
  into the fuel event pipeline.
- The pump type is picked by main and pushed to the DT board via
  `RX_ID_SET_DISPLAY` (`device.c:401-439`), stored in `settings.display`
  (persisted to SPIFFS).
- The firmwares keep **hand-duplicated copies** of the same enums/structs,
  and it's not just two copies — it's **three**:
  1. `ESP32-display-tap-src/distap-esp32/components/device/include/device.h`
     — the DT board itself (this repo/folder).
  2. `ESP32-display-tap-src/main-esp32/lib/device/{com_distap,cmd_distap,device,display_types}.{c,h}`
     — a standalone PlatformIO/Arduino **bench-test harness** for the main
     board + DT board pair (`src/main.cpp` here just calls
     `distap_get_fw_version()`/`distap_set_display_type()` directly and
     prints to `Serial`, no cloud/fuel pipeline). This is what "the
     production firmware" in point 3 is tested against on the bench.
  3. `src/sub-modules/ferp-device-firmware/ferp_board/main-esp32/lib/device/{com_distap,cmd_distap,device,display_types}.{c,h}`
     — the same files, vendored into the **production firmware**
     (`src/product/ferp-com-main`, driven by `src/app-modules/module_fuel`).

  Diffed (2) against (3): byte-identical except a board-pin header include
  (`board_inf.h` vs `board.h`) and one task stack size — i.e. these really
  are meant to be the same code, just not mechanically kept in sync.
  **Every protocol-level change in this plan (§4) must land in all three
  places.** Nothing today enforces that; it's on whoever implements this.

- **The "main application"** = `src/app-modules/module_fuel/` +
  `src/sub-modules/ferp-device-firmware/...main-esp32` (item 3 above) —
  the cloud-connected production firmware. Changes there are covered in
  §6.
- Firmware version string travels today already: main calls
  `distap_get_fw_version()` (`cmd_distap.c:90-116`) at startup, which reads
  `tx_id_dev_version_t.version` — that string comes straight from
  `PROJECT_VER`, which is just `CONFIG_APP_PROJECT_VER` from
  `sdkconfig`/`sdkconfig.defaults` (currently `"1.0.0.7"`). No code changes
  needed to *carry* a version string — only to set it and act on it.

## 2. Decisions (confirmed)

- **Separate firmware build**, not a runtime mode of the production
  firmware — smaller binary, only the raw-capture code compiled in (real
  pump decoders excluded).
- **Version scheme `99.x.y.z`** for this build, purely as a human-facing
  marker (a field engineer reading `distap_get_fw_version()`'s output
  knows a `99.x` DT board is a raw-capture test unit). **Main does not
  parse or act on this version string** — see next point.
- **No new logic on main to select or gate the display type.** Main keeps
  commanding the display type exactly the way it does today — same
  `RX_ID_SET_DISPLAY` command, same `distap_set_display_type()` call, same
  config path (`MsgConfigDT::Payload.display_type`). Main has no idea
  which pump decoders a given DT board was built with. Setting the
  "wrong" type for whatever's flashed (e.g. `0-7` on a `99.x` build, or
  `90+` on a normal build) is expected to be harmless: `device_init()`'s
  `switch` on `settings.display` (`device.c:72-100`) simply has no
  matching `case` for a type that build wasn't compiled with, falls to
  `default: break;`, and no display driver starts — so no data is ever
  sent for that channel. That's the existing behavior already, unchanged;
  we're just relying on it instead of adding a check on main.
- **New, separate wire message type** for raw data — not reusing
  `TX_ID_DIS1_DATA`/`TX_ID_DIS2_DATA` (which imply `display_data_t`-shaped
  decoded payload). Main registers **different callbacks** for raw data
  so it never enters the fuel/pump pipeline at all.
- **Chunked, paced transfer**: DT board collects a capture batch **per
  channel per data line** (both `SDATA1` and `SDATA2` are captured — see
  §0 for how this grew from the original single-line design below), sends
  it as 4 packets, then waits 2 seconds before capturing/sending the next
  batch. This exists because downstream (main → cloud log forwarding)
  can't keep up if raw data is streamed continuously. As actually
  implemented (§0/§4.2): 128 bytes per line, 32 bytes per packet — so
  each physical channel moves 256 bytes total per 2s cycle (128 × 2
  lines), and a full 2-second window moves ~512 bytes across both DIS1 and
  DIS2 combined. (This paragraph originally sized a single-line-only
  256-byte-per-channel batch before SDATA2 capture was added — the
  512-bytes-per-2s total turned out to land in the same place either way,
  just split differently.) Worth keeping in mind when sizing the
  log-forwarding path on main, but not something this DT-side plan needs
  to throttle further.

## 3. Why this design (and what it avoids)

Reusing `TX_ID_DIS1_DATA` + a `display>=90` sentinel (my earlier draft of
this plan) would have required auditing every place that already
casts that packet's payload straight to `display_data_t`/`app_display_data_t`
and feeds it into `pump_drivers[DIS_SIZE]` (`module_fuel.cpp:315-344,
520-636`) to make sure a raw payload could never reach it — an easy
place to introduce a crash (out-of-bounds array read) if one call site is
missed. Giving raw data its **own pck_id and its own callback**
sidesteps that entirely: raw bytes physically never touch
`ModuleFuel`/`pump_drivers[]`/`display_data_t`. That's a structural
guarantee instead of a runtime check, so it's the safer of the two designs
and is what this plan now goes with.

## 4. Wire protocol changes

**Applies to all three copies from §1** unless noted otherwise:
distap-esp32's `device.h`, and `com_distap.{c,h}` /
`display_types.h` in **both** `ESP32-display-tap-src/main-esp32/lib/device/`
(bench harness) and
`src/sub-modules/ferp-device-firmware/ferp_board/main-esp32/lib/device/`
(production). Do the edit once, diff the two main-esp32 copies to confirm
they still match (same trick as §1), then copy across — don't hand-type
it twice.

### 4.1 New packet IDs (append-only, same numeric value everywhere)

Append after the existing entries so no existing packet ID is renumbered
(both firmwares are versioned/paired anyway for this feature, but keeping
IDs append-only costs nothing and avoids surprises):

`distap-esp32/components/device/include/device.h` (`tx_pckt_id_t`) —
**as actually implemented**, 4 IDs, one per channel *and* data line (not
2 IDs + a shared field — see §0 for why this superseded an earlier
2-ID-plus-`data_line`-field cut):
```c
typedef enum
{
    TX_ID_DIS1_DATA = 0,
    TX_ID_DIS2_DATA,
    TX_ID_DIS_DATA_SIZE,
    TX_ID_DEV_INFO = TX_ID_DIS_DATA_SIZE,
    TX_ID_SET_DISPLAY,
    TX_ID_SET_ERR_MASK,
    TX_ID_INPUTS,
    TX_ID_KEEP_ALIVE,
    TX_ID_LOG_PRINTS,
    TX_ID_NACK,
    TX_ID_RAW_DIS1_L1_DATA, // channel 1, SDATA1
    TX_ID_RAW_DIS1_L2_DATA, // channel 1, SDATA2
    TX_ID_RAW_DIS2_L1_DATA, // channel 2, SDATA1
    TX_ID_RAW_DIS2_L2_DATA, // channel 2, SDATA2
    TX_ID_SIZE
} tx_pckt_id_t;
```

`com_distap.h` (`rx_pckt_id_t`) in **both** main-esp32 trees mirrors with
the same four new entries in the same position (after `RX_ID_NACK`, before
`RX_ID_SIZE`), so the wire values match exactly in all three files.

### 4.2 Raw chunk payload

```c
// shared shape, defined identically in device.h (distap) and
// display_types.h/com_distap.h in both main-esp32 trees
typedef struct __attribute__((packed))
{
    uint8_t  codeword_bits; // 8 or 12 — self-describing, so the log/tool
                            // knows how to interpret it without cross-
                            // referencing the display type separately
    uint16_t total_len;     // total bytes in this capture batch (128) —
                            // must be uint16_t, not uint8_t: an earlier
                            // 256-byte batch size didn't fit in a byte
                            // (max 255); the field stayed uint16_t after
                            // the batch size was later halved to 128, as
                            // cheap insurance against a future resize.
    uint8_t  chunk_index;   // 0..(chunk_count-1)
    uint8_t  chunk_count;   // number of chunks per batch (4)
    uint8_t  chunk_len;     // bytes in this chunk (32)
    uint8_t  data[];        // chunk_len raw bytes
} raw_capture_chunk_t;
```
No `data_line` field — which channel+line a batch belongs to is now
carried entirely by the pck_id (§4.1), so the payload doesn't need to
repeat it. 6-byte header + 32 bytes of data = 38 bytes per packet,
comfortably under `MAX_DATA` (128 bytes) — the 128-byte ceiling is a
**per-packet** limit (`data_packet_t.ab_data[]`, checked via
`offsetof(data_packet_t, ab_data) + length` throughout
`device.c`/`com_distap.c`), not a per-batch one. Log each chunk as it
arrives (tagged with index/total) rather than trying to reassemble the
full 128-byte batch before logging — simpler and doesn't lose the whole
batch if one chunk is dropped. Each of the 4 streams (DIS1×L1, DIS1×L2,
DIS2×L1, DIS2×L2) sends its own 4-chunk cycle independently; per physical
channel that's still 8 packets per pacing cycle (4 chunks × 2 lines), same
as before the batch size was halved — just 32 bytes of payload per packet
instead of 64, keeping total bandwidth per cycle where it was originally
budgeted (§0).

### 4.3 Still-needed validation fix (narrower than originally scoped)

Checked `cmd_distap.c:165-187` (`distap_set_display_type`): the command
packet's **header** `display` byte is always left at `0` — only the
*payload* (`tx_id_set_display_t.display`) carries the requested type. So
the generic `validate_packet()` header check (`packet->display < DIS_SIZE`)
does **not** block the `RX_ID_SET_DISPLAY` command path and does **not**
need to change on distap's receive side.

What *does* still need the range fix:
- `distap-esp32/components/device/device.c:403` — the `RX_ID_SET_DISPLAY`
  handler's inner check on the payload value:
  ```c
  if (((rx_id_set_display_t *)packet->ab_data)->display < DIS_SIZE)
  ```
  must become
  ```c
  if (((rx_id_set_display_t *)packet->ab_data)->display < DIS_SIZE ||
      is_raw_capture_type(((rx_id_set_display_t *)packet->ab_data)->display))
  ```
  otherwise main can never successfully command the DT board into type 90/91.
- `com_distap.c:310`'s `validate_packet()` — `packet->display < DIS_SIZE`
  check on **incoming** frames — **in both** main-esp32 trees (bench
  harness and production; identical code, identical fix). The new
  `TX_ID_RAW_DIS1_L1_DATA`/`_L2`/`TX_ID_RAW_DIS2_L1_DATA`/`_L2` packets
  will carry `display = 90/91` in their header (set the same way existing
  pump drivers hardcode `.display = DIS_SANKI_6_DIGIT` etc. at
  declaration), so this check must accept the raw range too or every raw
  chunk gets silently dropped as "invalid" on whichever main board you're
  testing with.

Add the shared helper next to both enums (all three files):
```c
#define DIS_RAW_TYPE_BASE   90
#define DIS_RAW_8BIT_V1     90
#define DIS_RAW_12BIT_V1    91
static inline bool is_raw_capture_type(uint8_t display) { return display >= DIS_RAW_TYPE_BASE; }
```

## 5. DT board (distap-esp32) changes

### 5.1 New component `components/raw_capture/`

*(Superseded in part by the dual-line design in §0 — SDATA1 and SDATA2 are
both captured, as fully independent streams, not SDATA1 alone. This section
otherwise still describes the actual implementation.)*

```c
esp_err_t display_raw_capture_init(QueueHandle_t *send_queue, uint8_t codeword_bits);
```
A single call sets up **both** physical channels (dis1/dis2) internally,
matching how every existing pump driver in this codebase is structured
(see §0's implementation-status note on this deviation from an earlier
per-channel-instance sketch), parametrized by `codeword_bits` (8 or 12) so
the same code serves both `DIS_RAW_8BIT_V1` and `DIS_RAW_12BIT_V1`.

Capture behaviour, reusing the existing GPIO/ISR pattern
(`SCLK`=bit clock, `RCLK`=frame latch, see `sanki_6_digit.c` for the
edge-interrupt style or `hongyang_8_digit.c` for the esp_timer-timeout-flush
style — pick whichever matches the unknown pump's observed signal
behaviour once probed on the bench):
- On each `SCLK` edge, shift one bit from **both** `SDATA1` and `SDATA2`
  into their own separate `codeword_bits`-wide accumulators per channel
  (both lines sampled on the same edge, but never mixed into one value).
- On `RCLK` edge (or timeout fallback), push **both** lines' completed
  codewords into their own separate linear capture buffers, advancing one
  shared buffer index per channel (both buffers always fill in lockstep,
  since the same `RCLK` edge latches both at once).
- **No decoding** — no digit maps, no index-nibble scheme, no price/volume
  math. Just raw codewords.
- **Static allocation only, no `malloc`/`free`.** Each channel has two
  128-byte capture buffers, one per data line — fixed
  `static uint8_t raw_buf_dis_1_l1[128]` / `raw_buf_dis_1_l2[128]` (etc.,
  four buffers total across both channels), matching every existing pump
  driver's `static capture_data_t capture_display_1 = {...}` pattern —
  nothing dynamic in the ISR or the send loop. The one `xQueueCreate()`
  per channel at init is the same FreeRTOS one-time heap allocation every
  existing driver already does (`capture_queue = xQueueCreate(...)` in
  `sanki_6_digit.c` etc.) — it's not libc `malloc`, it's sized once from
  `sdkconfig`'s FreeRTOS heap, and is never freed/re-created at runtime.
  The only `malloc` anywhere in this firmware today is in `settings.c`
  (one-off JSON buffer for SPIFFS load/save) and the cJSON allocator hooks
  in `main.c` — neither is on the capture/send path, and this feature
  doesn't add to that list.

Send state machine (per channel, both data lines), matching the final
128B/4×32B/2s spec (see §0 for how this superseded an earlier 256B/4×64B
cut once the design moved from 2 pck_ids+`data_line` to 4 dedicated
pck_ids):
```
loop:
  capture until both lines' buffers hold 128 bytes each (shared buf_idx
    per channel, since one RCLK edge always advances both together;
    disable capture ISR once full, same pattern as
    pin_interrupt_disable_dis_1() in existing drivers)
  for line in [SDATA1, SDATA2]:                  // pck_id picks (channel, line):
      pck_id = TX_ID_RAW_DIS{1,2}_L{1,2}_DATA     // e.g. TX_ID_RAW_DIS1_L1_DATA
      for chunk_index in 0..3:
          build raw_capture_chunk_t{ codeword_bits,
                                      total_len=128, chunk_index,
                                      chunk_count=4, chunk_len=32,
                                      data=line_buffer+chunk_index*32 }
          xQueueSend(send_queue, ...)           // reuses existing serial_send_task, pck_id set on data_packet_t
          vTaskDelay(10ms)                      // existing inter-packet gap
  vTaskDelay(2000ms)                             // pacing delay so main/cloud can keep up
  re-enable capture ISR, clear both buffers, repeat
```
Each channel (DIS1/DIS2) runs its own instance of this loop independently
— see §2 for the combined bandwidth figure across both channels and both
lines. This task looks structurally like `data_send_task()` in
`sanki_6_digit.c` (capture queue → format → send via the shared
`send_queue`/`serial_send_task`) — reuse that shape rather than inventing
a new one.

### 5.2 Wire into `device_init()` (`components/device/device.c:72-100`)

```c
case DIS_RAW_8BIT_V1:
    ret = display_raw_capture_init(&send_queue, 8);
    break;
case DIS_RAW_12BIT_V1:
    ret = display_raw_capture_init(&send_queue, 12);
    break;
```
(One call per type, not per channel — see §5.1's note on why the
per-channel-instance sketch wasn't what got implemented.)
(exact channel-2 wiring mirrors how existing drivers set up both
`dis_1`/`dis_2` instances in one `init()` call — follow that pattern.)

### 5.3 Smaller test-only build (Kconfig + component pruning)

1. Add a Kconfig bool in `components/device/` (e.g.
   `CONFIG_DISTAP_RAW_CAPTURE_ONLY`, default `n`).
2. `components/device/CMakeLists.txt` currently hardcodes
   `PRIV_REQUIRES spiffs json driver censtar hongyang longfeng sanki wayn`
   — gate the pump-component requirements out when building the raw-only
   variant:
   ```cmake
   if(CONFIG_DISTAP_RAW_CAPTURE_ONLY)
       idf_component_register(SRCS "spiff_mount.c" "device.c" "settings.c" "production_io.c"
           INCLUDE_DIRS "include"
           PRIV_REQUIRES spiffs json driver raw_capture)
   else()
       idf_component_register(SRCS "spiff_mount.c" "device.c" "settings.c" "production_io.c"
           INCLUDE_DIRS "include"
           PRIV_REQUIRES spiffs json driver censtar hongyang longfeng sanki wayn raw_capture)
   endif()
   ```
   (ESP-IDF makes `CONFIG_*` Kconfig values available directly as CMake
   variables inside component `CMakeLists.txt`, no extra plumbing needed.)
3. `components/device/device.c` — guard the pump-specific `#include`s and
   `case DIS_CENSTAR_*/DIS_HONGYANG_*/...` branches with
   `#if !CONFIG_DISTAP_RAW_CAPTURE_ONLY ... #endif`, leaving only
   `DIS_NONE`, `DIS_RAW_8BIT_V1`, `DIS_RAW_12BIT_V1` (and production_io)
   compiled into the raw-only build. This is what actually shrinks flash
   usage — pruning `PRIV_REQUIRES` alone doesn't help if `device.c` still
   references the pump symbols.
4. New `sdkconfig.raw_capture.defaults` (sits next to
   `sdkconfig.defaults`) setting:
   ```
   CONFIG_APP_PROJECT_VER_FROM_CONFIG=y
   CONFIG_APP_PROJECT_VER="99.0.0.1"
   CONFIG_DISTAP_RAW_CAPTURE_ONLY=y
   ```
5. New build/flash scripts alongside `flash.sh` (e.g.
   `build_raw_capture.sh` / `flash_raw_capture.sh`) that call
   `idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.raw_capture.defaults" build`
   into a separate build dir (`-B build_raw_capture`) so the normal
   production build/binary is never disturbed by switching configs back
   and forth.

## 6. Main-side changes (both `main-esp32` trees + the production app)

### 6.1 New raw callbacks, parallel to the existing fuel callbacks
### — protocol plumbing, identical edit in both main-esp32 trees

`com_distap.h`: extend `init_comms_distap()` with **4** raw callback
parameters, one per channel+line (not 2 — see §0):
```c
void (*dis1_l1_raw_cb)(const raw_capture_chunk_t *chunk);
void (*dis1_l2_raw_cb)(const raw_capture_chunk_t *chunk);
void (*dis2_l1_raw_cb)(const raw_capture_chunk_t *chunk);
void (*dis2_l2_raw_cb)(const raw_capture_chunk_t *chunk);
```
`com_distap.c` `read_response()`: add
```c
case RX_ID_RAW_DIS1_L1_DATA:
    if (dis1_l1_raw_cb) dis1_l1_raw_cb((const raw_capture_chunk_t *)packet->ab_data);
    break;
case RX_ID_RAW_DIS1_L2_DATA:
    if (dis1_l2_raw_cb) dis1_l2_raw_cb((const raw_capture_chunk_t *)packet->ab_data);
    break;
case RX_ID_RAW_DIS2_L1_DATA:
    if (dis2_l1_raw_cb) dis2_l1_raw_cb((const raw_capture_chunk_t *)packet->ab_data);
    break;
case RX_ID_RAW_DIS2_L2_DATA:
    if (dis2_l2_raw_cb) dis2_l2_raw_cb((const raw_capture_chunk_t *)packet->ab_data);
    break;
```
right next to the existing `RX_ID_DIS1_DATA`/`RX_ID_DIS2_DATA` cases —
same dispatch mechanism, disjoint from the fuel path. This is the same
file in both trees (§1) — one edit, copy to both, diff to confirm they
still match.

### 6.2 Production consumer (`src/app-modules/module_fuel`) — logging only

Four small functions in `fuel_disptap_driver.cpp` (`_dis1_l1_raw_event`,
`_dis1_l2_raw_event`, `_dis2_l1_raw_event`, `_dis2_l2_raw_event`) implement
the 4 raw callbacks: hex-dump `chunk->data[0..chunk_len)` tagged with
nozzle, line (passed in by the caller, since it's no longer a field on the
chunk itself — see §4.2), `chunk_index`/`chunk_count`, `codeword_bits`,
through the existing logger (`MLOG`/`pal_logger`) so it lands wherever
field logs already go (serial + `module_udp_log`, picked up today by
`tools/serial-log-viewer` / `tools/cloud-udp-monitor`). It must **not**
touch `ModuleFuel`, the `_frame_queue`, or `pump_drivers[]` — these
callbacks' only job is to log. This is "the main application" referred to
in §1/§2.

### 6.3 Bench-harness consumer (`ESP32-display-tap-src/main-esp32`)

`lib/device/device.cpp::initBoard()` already registers
`fuel_event_display_1`/`fuel_event_display_2` via `init_comms_distap()` —
today both are dead code (`return;` as their first line). Add
`raw_event_display_1_l1`/`_l2`/`raw_event_display_2_l1`/`_l2` alongside
them, registered through the extended (now 6-argument) `init_comms_distap()`
from §6.1, each doing a plain `Serial.println()` hex dump of the chunk (no
`pal_logger`/`MLOG` here — this tree is bare Arduino `Serial`, not the
production logging stack). This gives you a way to see raw capture output
directly on the bench without needing the full cloud-connected app
running.

### 6.4 Display type selection — unchanged

Both trees keep calling `distap_set_display_type(display_type)` exactly as
they do today — `fuel_disptap_driver.cpp::start()` in production
(`fuel_disptap_driver.cpp:85-87`) and the commented-out
`distap_set_display_type(...)` calls in bench-harness `main.cpp` — no
matter what `distap_get_fw_version()` reported. No new logic here — see
§2. Main stays completely unaware of which components a given DT board
was built with; that's entirely a property of which binary you flashed to
it.

## 7. Rollout order

1. Add the two enum/struct additions (`raw_capture_chunk_t`, new
   `TX_ID_RAW_*`/`RX_ID_RAW_*` ids, `is_raw_capture_type()`) identically to
   distap-esp32's `device.h` **and both** main-esp32 trees' `com_distap.h`
   / `display_types.h` — no behavior change yet, safe to land alone.
2. Fix `device.c:403` (DT-side `RX_ID_SET_DISPLAY` payload check) and
   `com_distap.c:310`'s `validate_packet()` **in both main-esp32 trees**,
   still a no-op until something actually uses type 90+.
3. Add `com_distap.c`/`.h` raw callback plumbing (§6.1) **in both
   main-esp32 trees**, then the two separate consumers: the logging-only
   one in `module_fuel`/`fuel_disptap_driver.cpp` (§6.2, production) and
   the `Serial.printf` one in bench-harness `device.cpp` (§6.3) — no-op
   until raw packets actually arrive.
4. Build `components/raw_capture/` for 8-bit first, wire into
   `device_init()`, gated behind the new Kconfig option.
5. Set up `sdkconfig.raw_capture.defaults` + build/flash scripts, produce
   the first `99.0.0.1` test binary, flash a bench DT board.
6. Bench-test first: pair the `99.0.0.1` DT board with the bench-harness
   main board (`ESP32-display-tap-src/main-esp32`), set `DIS_RAW_8BIT_V1`,
   confirm raw chunks show up over `Serial`. Cheaper to debug here than in
   the full cloud-connected production app.
7. Once the bench pairing works, point production's existing config at
   `DIS_RAW_8BIT_V1` (same config path as any other pump type — no main
   code changes needed for this step) and confirm: main logs chunked hex
   dumps via the new callbacks, and the normal fuel pipeline is never
   touched.
8. Repeat component work for 12-bit (`DIS_RAW_12BIT_V1 = 91`).
9. When a captured pump gets decoded, give it a real `DIS_*` entry in the
   dense 0..N range in the *production* firmware/build — the 90/91 slots
   stay reserved for the next unknown pump on the test build.

## 8. Open questions to confirm before implementing

- 256 bytes of 12-bit codewords — pack tightly (12 bits each, ~170
  codewords per batch) or store 1 codeword per 2 bytes for simplicity
  (128 codewords per batch, easier to eyeball in a hex log)? Packing
  tightly captures more history per batch; 2-bytes-per-codeword is
  trivially easier to read straight out of the log. Leaning toward the
  simple 2-bytes-per-codeword unless capture density turns out to matter.
- Should the 2-second pacing delay keep the capture ISR running (losing
  nothing, just delaying the *send*, at the cost of more RAM for a bigger
  buffer) or disable capture during the delay (simpler, matches existing
  drivers' `pin_interrupt_disable_dis_*` pattern, but drops signal during
  the gap)? Existing pump drivers all disable-while-processing, so that's
  the path of least resistance unless continuous capture turns out to be
  important for the pumps you're targeting.
