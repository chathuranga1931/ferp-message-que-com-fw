# Board checklist — ESP07 DisplayTap (used on ferp-com v1 board 2308 and v2 board 2404)

Go through this list for every DT build (test and release). The DT is flashed only by the main
board (UART, esp-serial-flasher, image stored on the main's SD card).

| | |
|---|---|
| MCU | ESP8266 (ESP-07), ESP8266 RTOS SDK v3.4 |
| Main board side | `main-esp32/lib/device/board_2308.h` / `board_2404.h` (`RESET_DISTAP` = main GPIO0, `IO0_DISTAP` = main GPIO4, input except while flashing) |
| Versions | release `20.x.y.z` (firmware type 20; earlier 2.0.0.1, 3.1.0.1); test builds `20.x.999.n` |

## Pins

| ESP07 GPIO | Use | Notes |
|---|---|---|
| **0** | Display 2 `D2_SDATA2` **and boot strap** (also main GPIO4 for flashing) | **Low at reset = download mode** |
| **2** | Display 2 `D2_SDATA1` **and boot strap** | Must be high at reset |
| **15** | `DIS_ENB` **and boot strap** | Must be low at reset (high = SD-card boot mode) |
| 14, 13, 16, 12 | Display 1: SCLK, SDATA1, SDATA2, RCLK | |
| 4, 5 | Display 2: SCLK, RCLK | |
| 1 / 3 | UART to the main | Also the ROM/flash UART |

## Known issues (notes)

- **2026-10-13 — all three ESP8266 strapping pins are on the display bus.** A hardware reset while
  pump 2's bus drives GPIO0/GPIO2 low (or GPIO15 high) boots the ESP07 into the wrong mode: no
  frames until the next reset. The main therefore does not reset the ESP07 after its settings and
  has no frame watchdog on 2308/2404 (only on 2602). Fix in hardware on the next revision
  (dedicated strap nets, or bus buffers disabled while in reset).
- **Wayne-6:** disabled in 3.1.0.1 / 20.1.0.1 (bench: volume/total 0 for ~1 min after boot with the
  emulator), re-enabled 2026-10-15 because it works on site (as in 2.0.0.1). Check the first sale after a boot.
- **Not supported by this DT:** Censtar-7 CS, Longfeng — `display_supported()` refuses; Wayne-2 has no
  decoder (type 8 is beyond DIS_SIZE) —
  them and starts no decoder.
- **Version string** must come from the app descriptor (`esp_ota_get_app_description()->version`);
  `CONFIG_APP_PROJECT_VER` differed from `-D PROJECT_VER` and made the main re-flash the DT at
  every boot.
- **Flash verify is not supported** on the ESP8266 by esp-serial-flasher; the main treats
  verify-unsupported as success and checks the version read back instead.

## Per-build checklist

- [ ] Version in `sdkconfig` and `sdkconfig.defaults` = the reported version.
- [ ] No new use of GPIO 0, 2, 15 (they are display inputs and boot straps).
- [ ] Flashed from the main (DT OTA → main reboot): `DT update: done, DT runs <version>`.
- [ ] Supported types (CEN6, CEN7, HY8, WAY6, SAN6): after a type change + main reboot, frames
      carry the new type; unsupported types are refused.
- [ ] Acceptance matrix on a v2 test device — see `src/product/ferp-com-v2-main/BOARD_CHECKLIST.md`.
