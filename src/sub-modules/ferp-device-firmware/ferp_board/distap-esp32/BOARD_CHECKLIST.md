# Board checklist — ESP32 DisplayTap (used on ferp-com v3, board 2602)

Go through this list for every DT build (test and release). The DT is flashed only by the main
board (UART, esp-serial-flasher, image stored on the main's SD card), so a DT that does not boot
its app cannot be recovered in the field without the main.

| | |
|---|---|
| MCU | ESP32, build with ESP-IDF 5.5.3 |
| Main board side | `main-esp32/lib/device/board_2602.h` (`RESET_DISTAP` = main GPIO0, `IO0_DISTAP` = main GPIO4) |
| Versions | release `21.x.y.z` (firmware type 21; earlier 1.0.0.9, 3.1.0.1); test builds `21.x.999.n` |

## Pins

| DT GPIO | Use | Notes |
|---|---|---|
| **0** | Boot strap, driven by main GPIO4 (`IO0_DISTAP`) | **Low at reset = ROM download mode** (no app, no frames). Main must keep it high except while flashing. |
| EN | Reset, driven by main GPIO0 (`RESET_DISTAP`) | |
| 1 / 3 | UART0 TX / RX to the main | Also the ROM/flash UART |
| 2 | `DIS_LED` | Strap (must be low/floating for download mode) — output only after boot |
| 13, 14, 4, 27, 33, 25 | Display 1: SCLK, SDATA1, SDATA2, RCLK, IN_CS, OUT_CS | |
| 21, 18, 19, 22, 32, 26 | Display 2: SCLK, SDATA1, SDATA2, RCLK, IN_CS, OUT_CS | |
| 23 | `DIS_ENB` | |
| 12, 15 | not used | Straps (flash voltage / boot log) — keep free |

## Known issues (notes)

- **2026-10-13 — DT IO0 was shared with LED2 on the main board 2602.** LED2 blinking made every
  DT reset a coin flip between app and download mode. Fixed on the main side (LED2 not driven).
- **Capture stops after a software restart.** The DT restarts itself (`esp_restart`) when the
  display type or the error mask changes; afterwards several decoders send no frames until a
  hardware reset (seen on 1.0.0.9: CEN7, C7CS, HY8, WAY6, SAN6, WAY2). The main resets the DT
  after sending its settings and runs a frame watchdog to cover this. A DT-side fix (re-init
  of the capture peripherals, or applying settings without a restart) would remove the need.
- **The version reply's `display` field lags** right after a restart (reports 0 or the old type);
  do not use it to confirm a type change — use the type carried by the data frames.
- **Longfeng needs SPI mode 3** (`components/longfeng`); mode 0 drops the last word (fixed in 3.1.0.1).

## Per-build checklist

- [ ] Built with IDF 5.5.3; `PROJECT_VER` = the version reported in the version reply.
- [ ] No new use of GPIO 0, 2, 12, 15 as outputs/pulls active at reset.
- [ ] Flashed from the main (DT OTA → main reboot): `DT update: done, DT runs <version>`.
- [ ] Hardware reset from the main boots the app (version answers within 1 s).
- [ ] Every display type: after a type change + main reboot, frames carry the new type.
- [ ] Acceptance matrix on the v3 test device (all 8 types, 2 reboots, 2 sales) — see
      `src/product/ferp-com-v3-main/BOARD_CHECKLIST.md`.
