# Board checklist — ferp-com v3 (board 2602, ESP32 DisplayTap)

Go through this list for every build of this product (test and release). Any pin change, new
peripheral or new module that touches a GPIO must be checked against the pin table first.

| | |
|---|---|
| Main MCU | ESP32 (board 2602, `BOARD_2602` in `app/user_config.h`, `HW_VERSION "2602"`) |
| DisplayTap | ESP32 DT — firmware `ferp_board/distap-esp32` (see its `BOARD_CHECKLIST.md`) |
| Pin map | `ferp_board/main-esp32/lib/device/board_2602.h`, app pins in `app/app_hw_config.h` |
| Versions | release `3.x.y.z` (first number = firmware type 3); test builds `3.x.999.n`; passed releases get a `-stable` tag |

## Shared and strapping pins (main ESP32)

| GPIO | Use on this board | Why it is sensitive |
|---|---|---|
| **4** | `IO0_DISTAP` = the DT's **IO0 boot strap** — also wired to `ESP_LED2` | Low while the DT is reset → the DT boots into its ROM download mode and sends nothing. **Must stay high except while flashing the DT.** |
| 0 | `RESET_DISTAP` (DT reset, high = reset) | Main's own boot strap (low at main reset = main download mode) |
| 2 | `EN_4G` | Main strap (must be low/floating for main download mode) |
| 5 | `ESP_LED1` | Main strap (SDIO timing) |
| 12 | `OUTPUT5` | Main strap MTDI: high at main reset selects 1.8 V flash → main does not boot |
| 15 | `SPI_CS_SD` | Main strap MTDO (boot log / timing) |

## Known issues (notes)

- **2026-10-13 — GPIO4 is both LED2 and the DT's IO0 strap.** `module_leds` blinked LED2 at 4 Hz.
  Every DT hardware reset (the comms "No reply, resetting distap" auto-reset, the type-change
  reset) then had about a 50 % chance of booting the DT into download mode: no frames, no sales,
  random results per pump type. Fix: LED2 is not driven (`LED2_GPIO` not defined in
  `app/app_hw_config.h`), so board_init keeps GPIO4 high. Only because of this,
  `BOARD_DT_RESET_SAFE` is defined in `board_2602.h`, which enables the DT reset after its
  settings at boot and the frame watchdog (`fuel_disptap_driver.cpp`).
  **Do not drive GPIO4 from any module.** If LED2 is wanted again, move it to a free pin on the
  next board revision.
- **DT 1.0.0.9 / 3.1.0.1 capture stops after the DT restarts itself** (it does that when its
  display type or error mask changes). Recovery needs a DT hardware reset — safe here only
  because of the note above.
- **DT command replies:** the set-display / set-error-mask commands must wait > 1 s (they are
  re-sent every second); a 500 ms wait lost the setting when one packet was dropped.

## Per-build checklist

**Pins**
- [ ] No new code drives GPIO4 (search: `IO0_DISTAP`, `ESP_LED2`, `LED2_GPIO`, `GPIO_NUM_4`, `gpio 4`).
- [ ] No new output or pull on a strapping pin (0, 2, 5, 12, 15) that is active during reset.
- [ ] `app/app_hw_config.h` pins still match `board_2602.h`.

**Build identity**
- [ ] `BOARD_2602` and `HW_VERSION "2602"`; version follows `3.x.y.z` (test `3.x.999.n`).
- [ ] `TEST_PUMP_BUILD` disabled for releases (real MAC as MQTT client id / cloud identity).
- [ ] Default MQTT host `mqtt.ferp.healosys.com` in `app/app.cpp` and `data/Configs/DeviceConfigs.json`.

**Boot (UART log)**
- [ ] `DT board FW version: …` read at boot.
- [ ] `DT update: DT already runs the stored image …` (or a successful update, max 3 attempts).
- [ ] `DT hardware-reset after its settings (answered before, answers after; …)`.
- [ ] Frames arrive (`Received frame` with the configured type) within ~10 s of `started`.

**Pump types** (Mega emulator, acceptance test: switch type, 2 reboots, 2 sales on both nozzles,
values equal to the emulator)
- [ ] With the DT image that ships: CEN6, CEN7, C7CS, HY8, WAY6, SAN6, LF8, WAY2.
- [ ] No `DT watchdog` line during a steady sale (it may appear right after a type switch).

**Before release**
- [ ] OTA from the previous release works; config survives the update.
- [ ] Results recorded on the web Releases page (versions, compatibility, test run).
