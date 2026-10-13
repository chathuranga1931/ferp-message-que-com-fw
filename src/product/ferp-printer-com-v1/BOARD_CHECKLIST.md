# Board checklist — ferp-printer COM v1 (board 2308, ESP32, ESC/POS over UART)

Go through this list for every build of this product (test and release). Any pin change, new
peripheral or new module that touches a GPIO must be checked against the pin table first.

| | |
|---|---|
| MCU | ESP32 on board 2308 (`HW_VERSION "2308"`); no DisplayTap (`app/board.h` is empty) |
| Pin map | `app/app_hw_config.h` (matches the legacy `printer_2308.h`) |
| Versions | release `44.x.y.z`; OTA target `printer-com-main` |

## Pins and strapping pins

| GPIO | Use | Why it is sensitive |
|---|---|---|
| 17 / 16 | Printer UART2 TX / RX | — |
| **12** | Buzzer (active high) | ESP32 strap MTDI: **high at reset selects 1.8 V flash → the ESP32 does not boot**. The buzzer driver must not pull GPIO12 high during reset/boot; only drive it after start-up. |
| 13 | Print button (present, not used) | — |
| 0, 2, 5, 15 | not used by the app | ESP32 strapping pins — keep them free of outputs active during reset |

## Known issues (notes)

- **2026-10-13 — shared / strapping pins must be listed per board.** On the ferp-com v3 board a
  status LED shared a pin with the DisplayTap's boot strap and silently broke every DT reset.
  This board has no DT, but GPIO12 (buzzer) is a boot strap of the ESP32 itself: never configure
  a pull-up or a default-high level on it.

## Per-build checklist

**Pins**
- [ ] Buzzer GPIO12 is configured as an output driven low until a beep (no pull-up, no default high).
- [ ] No new output or pull on a strapping pin (0, 2, 5, 12, 15) that is active during reset.

**Build identity**
- [ ] Version `44.x.y.z`, OTA target `printer-com-main`, MQTT dev type `ferp-printer`.
- [ ] Default MQTT host `mqtt.ferp.healosys.com` in `app/app.cpp`.

**Function**
- [ ] Device boots after a power cycle with the printer connected (checks the GPIO12 strap).
- [ ] Test print over MQTT and over the HTTP API (port 80).
- [ ] Print failure beeps the buzzer.

**Before release**
- [ ] OTA from the previous release works; config survives the update.
- [ ] Results recorded on the web Releases page.
