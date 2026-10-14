# Board checklist — ferp-com v1 (board 2308-modified, ESP07 DisplayTap)

Go through this list for every build of this product (test and release). Any pin change, new
peripheral or new module that touches a GPIO must be checked against the pin table first.

| | |
|---|---|
| Main MCU | ESP32 (board 2308-modified, `BOARD_2308` in `app/user_config.h`, `HW_VERSION "2308"`; production-test board id 2303) |
| DisplayTap | ESP07 (ESP8266) DT — firmware `ferp_board/distap-esp07` (see its `BOARD_CHECKLIST.md`) |
| Pin map | `ferp_board/main-esp32/lib/device/board_2308.h`, app pins in `app/app_hw_config.h` |
| Versions | release `1.x.y.z` (first number = firmware type 1); test builds `1.x.999.n`; passed releases get a `-stable` tag |

## Shared and strapping pins (main ESP32)

| GPIO | Use on this board | Why it is sensitive |
|---|---|---|
| **4** | `IO0_DISTAP` = the ESP07's **GPIO0 boot strap**, which on the ESP07 is also display 2 data line (`D2_SDATA2`) | Input on main except while flashing the ESP07. Never drive it otherwise. |
| **0** | `RESET_DISTAP` (ESP07 reset, **inverted on this board: low = reset**; board_init holds the ESP07 in reset until the fuel module starts it) | Main's own boot strap (low at main reset = main download mode) |
| 5 | `RESET_ESP32` | Not driven by the firmware |
| 2 | `EN_4G` | Main strap (must be low/floating for main download mode) |
| 12 | `OUTPUT5` | Main strap MTDI: high at main reset selects 1.8 V flash → main does not boot |
| 15 | `SPI_CS_SD` | Main strap MTDO (boot log / timing) |

No LEDs on this board.

## Known issues (notes)

- **2026-10-13 — the ESP07's strapping pins are display inputs.** GPIO0 (`D2_SDATA2`), GPIO2
  (`D2_SDATA1`) and GPIO15 (`DIS_ENB`) are all wired to the pump-2 display bus. If the ESP07 is
  hardware-reset while that bus drives GPIO0 or GPIO2 low (or GPIO15 high), it boots into
  download / wrong mode and sends nothing until the next reset. Therefore this board does **not**
  define `BOARD_DT_RESET_SAFE`: main does not reset the ESP07 after its settings and has no frame
  watchdog (those exist only on 2602). The comms 30 s "No reply, resetting distap" auto-reset is
  still active — same risk, rarely hit. Fix in hardware on the next revision (strap pins on
  dedicated nets, or a buffer that is disabled during reset).
- **Reset polarity differs from 2404/2602** (`gpio_set_reset_distap()` inverts it in `board_2308.c`). Always use that function, never `gpio_set_level(RESET_DISTAP, …)` directly.
- **On 2602 the same DT IO0 net was shared with LED2** and broke DT resets — never add an LED or
  other output on GPIO4 here either.
- **DT command replies:** the set-display / set-error-mask commands must wait > 1 s (re-sent
  every second); a 500 ms wait lost the setting when one packet was dropped.
- **The ESP07 DT does not support** Censtar-7 CS and Longfeng (no decoder starts). Wayne-6 was off in
  3.1.0.1 / 20.1.0.1 and is supported again from the next ESP07 release.

## Per-build checklist

**Pins**
- [ ] No new code drives GPIO4 or GPIO0 outside the DT reset / flashing sequence.
- [ ] No new output or pull on a strapping pin (0, 2, 12, 15) that is active during reset.
- [ ] `app/app_hw_config.h` pins still match `board_2308.h`.

**Build identity**
- [ ] `BOARD_2308` and `HW_VERSION "2308"`; version follows `1.x.y.z` (test `1.x.999.n`).
- [ ] `TEST_PUMP_BUILD` disabled for releases (real MAC as MQTT client id / cloud identity).
- [ ] Default MQTT host `mqtt.ferp.healosys.com` in `app/app.cpp` and `data/Configs/DeviceConfigs.json`.

**Boot (UART log)**
- [ ] `DT board FW version: …` read at boot.
- [ ] `DT update: DT already runs the stored image …` (or a successful update, max 3 attempts;
      the ESP07 cannot verify flash, verify-unsupported is treated as success).
- [ ] Frames arrive (`Received frame` with the configured type) after `started`.

**Pump types** (Mega emulator, acceptance test: switch type, 2 reboots, 2 sales on both nozzles,
values equal to the emulator)
- [ ] ESP07 DT supported types: CEN6, CEN7, HY8, WAY6, SAN6 (no Wayne-2 decoder on the ESP07: type 8 is refused).
- [ ] Unsupported types (C7CS, LF8, WAY2) are refused by the DT, not decoded wrongly.

**Before release**
- [ ] OTA from the previous release works; config survives the update.
- [ ] Results recorded on the web Releases page (versions, compatibility, test run).
