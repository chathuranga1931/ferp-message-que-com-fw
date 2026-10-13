# Board checklist — ferp-printer USB v1 (ESP32-S3, ESC/POS over USB host)

Go through this list for every build of this product (test and release). Any pin change, new
peripheral or new module that touches a GPIO must be checked against the pin table first.

| | |
|---|---|
| MCU | ESP32-S3 (`HW_VERSION "S3-USB"`); no DisplayTap (`app/board.h` is empty) |
| Pin map | `app/app_hw_config.h` — no GPIOs configured by the app |
| Versions | release `45.x.y.z`; OTA target `printer-usb-main` |

## Pins and strapping pins

| GPIO | Use | Why it is sensitive |
|---|---|---|
| 19 / 20 | Native USB D− / D+ (printer, USB OTG host) | Used by the USB host library — never reassign |
| 0, 3, 45, 46 | not used | ESP32-S3 strapping pins (boot mode, JTAG source, flash voltage, ROM log) — keep free of outputs or pulls active during reset |

No buzzer or button is defined yet (see the comment in `app/app_hw_config.h`).

## Known issues (notes)

- **2026-10-13 — shared / strapping pins must be listed per board.** On the ferp-com v3 board a
  status LED shared a pin with the DisplayTap's boot strap and silently broke every DT reset.
  When a buzzer, button or LED is added here, put it in the table above first and keep it off
  the S3 strapping pins and the USB pins.

## Per-build checklist

**Pins**
- [ ] Any new GPIO is in the table above and is not 0, 3, 19, 20, 45 or 46.

**Build identity**
- [ ] Version `45.x.y.z`, OTA target `printer-usb-main`, MQTT dev type `ferp-printer`.
- [ ] Default MQTT host `mqtt.ferp.healosys.com` in `app/app.cpp`.

**Function**
- [ ] USB printer is detected after boot and after re-plugging.
- [ ] Test print over MQTT and over the HTTP API (port 80).

**Before release**
- [ ] OTA from the previous release works; config survives the update.
- [ ] Results recorded on the web Releases page.
