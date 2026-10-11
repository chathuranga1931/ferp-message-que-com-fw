# disp_emulator_mega

Arduino Mega sketch that **emulates the serial data bus of real fuel-dispenser display
controllers** (SANKI and CENSTAR families). It is used as a hardware-in-the-loop tester
for the FERP COM board — the COM board receives these display packets exactly as if a
real dispenser were attached, so its decoding logic can be verified without a physical
dispenser present.

---

## Hardware Required

| Component | Notes |
|---|---|
| Arduino Mega 2560 | Any clone works |
| SSD1306 OLED 128×64 | I²C address `0x3C` |
| 4× front-panel push buttons | Up / Down / Enter / Back (menu) |
| 2× LEDs (red / green) | With current-limiting resistors |
| Nozzle toggle switch + pumping button | Nozzle status (pin 52) + Pumping (pin 50) |
| Jumper wires to FERP COM board | See pin table below |

---

## Pin Assignments

| Arduino Pin | Signal | Direction | SANKI role | CENSTAR role | Notes |
|---|---|---|---|---|---|
| 2 | SCLK1 | Output | Clock channel 1 | Shift clock (SCLK) | Direct AVR port write (PORTE) |
| 3 | SDATA1 | Output | Data channel 1 | Serial data (SDATA) | Direct AVR port write (PORTG) |
| 4 | SCLK2 | Output | Clock channel 2 | — | Unused in current firmware |
| 5 | SDATA2 | Output | Data channel 2 | — | Unused in current firmware |
| 6 | CS / RCLK | Output | Chip-select toggle | Register-latch clock | Toggles after each data byte |
| 10 | NOZZLE1\_IO | Output | Nozzle 1 status | Nozzle 1 status | HIGH = nozzle up (remapped from 11 — hardware fault) |
| 12 | NOZZLE2\_IO | Output | Nozzle 2 status | Nozzle 2 status | HIGH = nozzle up |
| 38 | BTN\_BACK | Input | — | — | Running: back to the display-type menu (restarts); active LOW, INPUT\_PULLUP |
| 40 | BTN\_ENTER | Input | — | — | Start the selected display type; active LOW, INPUT\_PULLUP |
| 42 | BTN\_DOWN | Input | — | — | Menu: next display type; active LOW, INPUT\_PULLUP |
| 44 | BTN\_UP | Input | — | — | Menu: previous display type; active LOW, INPUT\_PULLUP |
| 46 | GREEN\_LED | Output | — | — | Blinks while pumping active |
| 48 | RED\_LED | Output | — | — | ON while nozzle is up |
| 50 | BTN\_PUMPING | Input | — | — | Hold LOW to simulate active pumping; INPUT\_PULLUP |
| 52 | BTN\_NZL\_STATUS | Input | — | — | Hold LOW to simulate nozzle lifted; INPUT\_PULLUP |

---

## Startup — Configuration Phase

1. The OLED shows **"Configuring… / Display Mode:"** and the currently selected type.
2. Press **Up** / **Down** to scroll through the supported display types:
   - `SANKI-6-DIGIT`
   - `CENSTAR-6-DIGIT`
   - `CENSTAR-7-DIGIT` *(not implemented — WDT reset if selected)*
   - `WAYNE-6-DIGIT` *(not implemented — WDT reset if selected)*
   - `HONGYANG-8-DIGIT` *(not implemented — WDT reset if selected)*
3. Press **Enter** to confirm. The selected protocol driver is initialised
   and the OLED switches to **"Running… / Display Mode: \<type\>"**.

---

## Running Phase — Simulated Dispenser

After the display type is confirmed, `loop()` calls the appropriate driver every
iteration. Both drivers share the same button-controlled simulation logic:

| Button | Action |
|---|---|
| BTN\_NZL\_STATUS held LOW | Nozzle "up" — volume resets to 0, outputs NOZZLE1/2\_IO HIGH |
| BTN\_PUMPING held LOW (with nozzle up) | Volume increments continuously, green LED blinks |
| Both released | Volume frozen, green LED off |

- **Red LED** mirrors the nozzle-up state.
- **Green LED** blinks at each packet update interval while pumping is active.
- Serial (115 200 baud) prints the computed Vol / Unit / Total values on every buffer
  update for debugging.

---

## Protocol Details

### SANKI 6-digit

**Packet size:** 18 bytes, sent continuously in every `loop()` call.

**Buffer layout (indices 0–17):**

```
byte[ 0]  = 0xF0                              (sync / header)
byte[ 1]  = unit_price_digit[1] | 0x00        (MSB of unit price, low nibble = 0)
byte[ 2]  = volume_digit[2]     | (17-2)      ┐
byte[ 3]  = volume_digit[3]     | (17-3)      │  6 bytes
byte[ 4]  = volume_digit[4]     | (17-4)      │  volume
byte[ 5]  = volume_digit[5]     | (17-5)      │  (milli-litres)
byte[ 6]  = volume_digit[6]     | (17-6)      │
byte[ 7]  = volume_digit[7]     | (17-7)      ┘
byte[ 8]  = total_digit[2]      | (17-8)      ┐
byte[ 9]  = total_digit[3]      | (17-9)      │  6 bytes
byte[10]  = total_digit[4]      | (17-10)     │  total amount
byte[11]  = total_digit[5]      | (17-11)     │
byte[12]  = total_digit[6]      | (17-12)     │
byte[13]  = total_digit[7]      | (17-13)     ┘
byte[14]  = unit_price_digit[2] | (17-14)     ┐
byte[15]  = unit_price_digit[3] | (17-15)     │  4 bytes
byte[16]  = unit_price_digit[4] | (17-16)     │  unit price
byte[17]  = unit_price_digit[5] | (17-17)     ┘
```

Each byte is packed: **high nibble = BCD digit, low nibble = descending position
counter (17 − byte_index)**.  `0xFF` in the digit slot means "blank / no digit".

**Transmission order and timing:**

```
CS LOW ──────────────────────────────────────────────────────────────
                │100µs│        byte1       │100µs│        byte2 ... byte17
               [byte0 clocked out]         [each byte followed by CS HIGH→LOW pulse]
```

- Clock: LOW for 7 µs (2+5), HIGH for 5 µs — MSB first.
- After byte 0 and byte 1: 100 µs inter-byte gap.
- After bytes 2–17: CS toggles HIGH 5 µs → LOW 5 µs after each byte.
- Direct port manipulation (`PORTE`, `PORTG`, `PORTH`) is used for timing accuracy.

**Simulation increments:** +125 mL every 100 ms while pumping (≈ 75 L/min).  
**Hardcoded unit price:** 42 300 (represents 423.00 currency units per litre × 100).

---

### CENSTAR 6-digit

**Packet size:** 16 bytes, sent every 50 ms.

**Buffer layout (indices 0–15), MSB of each field first in packet:**

```
byte[ 0] = unit_price_digit[4] | 0   ┐
byte[ 1] = unit_price_digit[3] | 1   │  4 bytes — unit price (digits 4..1)
byte[ 2] = unit_price_digit[2] | 2   │
byte[ 3] = unit_price_digit[1] | 3   ┘
byte[ 4] = total_digit[6]      | 4   ┐
byte[ 5] = total_digit[5]      | 5   │  6 bytes — total amount (digits 6..1)
byte[ 6] = total_digit[4]      | 6   │
byte[ 7] = total_digit[3]      | 7   │
byte[ 8] = total_digit[2]      | 8   │
byte[ 9] = total_digit[1]      | 9   ┘
byte[10] = volume_digit[7]     | 10  ┐
byte[11] = volume_digit[6]     | 11  │  6 bytes — volume (digits 7..2)
byte[12] = volume_digit[5]     | 12  │
byte[13] = volume_digit[4]     | 13  │
byte[14] = volume_digit[3]     | 14  │
byte[15] = volume_digit[2]     | 15  ┘
```

Each byte is packed: **high nibble = BCD digit, low nibble = ascending byte index
(0–15)**.  `0x00` in the digit slot means "blank / no digit".

**Transmission — `send_byte_censtar_6()`:**

```
For each bit (MSB first):
  1. Set SDATA1 to bit value
  2. SCLK LOW  (15 µs)
  3. SCLK HIGH
  4. If last bit of byte: RCLK HIGH (14 µs) → RCLK LOW (48 µs)
     Otherwise: wait 36 µs
```

- RCLK (pin 6) latches the shift register into the display at the end of every byte.
- All 16 bytes are sent back-to-back in a single `send_byte_censtar_6()` loop.
- The entire 16-byte packet is re-sent every 50 ms.

**Simulation increments:** +49 mL every 100 ms while pumping (≈ 29.4 L/min).  
**Hardcoded unit price:** 12 345 (represents 123.45 currency units per litre × 100).

---

## Value Computation

```
total = (volume_ml / 1000.0) × unit_price_x100
```

All three values — **volume**, **unit price**, and **total** — are decomposed into
individual BCD decimal digits before packing into the transmit buffer.  Leading digits
that are out of range are filled with `0xFF` (SANKI, → blank segment) or `0x00`
(CENSTAR, → blank segment).  A minimum digit count is enforced so that at least 3
(price/total) or 4 (volume) digits are always displayed.

---

## Front-panel buttons

- **Pins:** Up = 44, Down = 42, Enter = 40, Back = 38 (all active LOW with INPUT\_PULLUP), set in `board_mega.h`.
  Earlier versions of this sketch used a rotary encoder on 18 / 19 with the button on 17.
- `Encoder.cpp` keeps the old encoder interface (`getEncoderPosition()`): each **Up** press moves the
  menu cursor back one step and each **Down** press forward one step (30 ms debounce, one step per press),
  clamped to `[0, pump_type_count − 1]`.
- **Enter** starts the selected display type; **Back** (while running) restarts the board into the menu.

---

## OLED Display (SSD1306 128×64, I²C)

| Phase | Content |
|---|---|
| Configuring | "Configuring… / Display Mode: \<selected type\>" |
| Running | "Running… / Display Mode: \<selected type\>" |

Signal-strength icon bitmaps (5 levels, 8×16 px) are stored in PROGMEM and a
`drawSignalStrength(x, y, strength)` helper is available but not actively called in
the current code (reserved for future use).

---

## Serial Debug Output (115 200 baud)

While pumping, each buffer-update prints:

```
VOL: 1250   UNIT: 42300   Total: 52875
```

With `SHOW_SANKI_BUFFER_GEN_DEGUB` or `SHOW_CENSTAR_BUFFER_GEN_DEGUB` defined,
the raw byte arrays are also printed in hex.

---

## Known Limitations

- Only `SANKI-6-DIGIT` and `CENSTAR-6-DIGIT` are fully implemented.
  Selecting any other type triggers a WDT reset (intentional guard).
- Censtar `buffer_idx` is always 0 — the secondary-channel offset is not used.
- Volume and unit price are **compile-time constants**; they cannot be changed at
  runtime without reflashing.
- The `SDATA2` / `SCLK2` lines are wired but carry no data in the current firmware.

---

## Remote control over USB serial (115200 baud)

The PC can drive the emulator without anyone at the panel. One command per line (ASCII, `\n` or `\r\n`,
case-insensitive). Every command gets one reply line, `OK …` or `ERR …`; the emulator also sends
unprompted `EVT …` lines. Full description in `control.h`.

| Command | Effect |
|---|---|
| `PING` | `OK PONG` |
| `STATUS` | `OK STATUS mode=SAN6 nozzle=1 pump=1 vol_ml=3500 price=30000 total=105000 step_ml=250 ctl_nozzle=pc ctl_pump=pc` |
| `MODE SAN6` / `MODE CEN6` | Start that display type (restarts the board when switching) |
| `MENU` | Back to the display-type menu (same as the Back button) |
| `NOZZLE 1` / `0` / `AUTO` | Nozzle up / down from the PC; `AUTO` hands it back to the switch (pin 52) |
| `PUMP 1` / `0` / `AUTO` | Pumping on / off from the PC; `AUTO` hands it back to the button (pin 50) |
| `PRICE <x100>` | Unit price, e.g. `PRICE 42300` = 423.00 |
| `VOLUME <ml>` | Current sale volume |
| `STEP <ml>` | Volume added every 100 ms while pumping (Sanki default 125, Censtar 49) |
| `BUTTON UP` / `DOWN` / `ENTER` / `BACK` | Press a front-panel button from the PC |
| `DEBUG 1` / `0` | The drivers' raw buffer printouts (off by default) |
| `HELP` | List of commands |

Events: `EVT READY`, `EVT MODE SAN6`, `EVT MENU`, `EVT MENU_IDX n`, `EVT BUTTON UP|DOWN|ENTER|BACK|NOZZLE|PUMP`
(front-panel presses), `EVT NOZZLE 0|1`, `EVT PUMP 0|1`, `EVT ZERO` (volume reset when the nozzle goes up).

`MODE` and `MENU` restart the board, so values set from the PC (`PRICE`, `STEP`, `VOLUME`) go back to the
display type's defaults; set them again after switching.

## Status screen (running)

```
SAN6      NOZ UP        <- yellow band: display type, nozzle and pumping state
          PMP ON
Vol   12.375 L
Price 423.00
Total 5234.62
Ctl sw/btn              <- who controls nozzle / pumping: switch & button, or PC
Last PUMP button        <- last button pressed or PC command
```

The screen is redrawn only when something changes, at most 4 times a second (a full redraw takes about
25 ms on I²C, during which the display bus output pauses).

---

## More display types (`pumps_extra.cpp`)

Ported from `esp-device-firmware-new/display_simulator/` and mapped onto this board's outputs
(simulator SCLK → pin 2, SDATA1 → 3, SDATA2 → 5, RCLK → 6). Select them in the menu or with `MODE`:

| Menu entry | `MODE` | Device DISPLAY_TYPE | Bus | Tested with the COM test device (ESP32 DT 1.0.0.9) |
|---|---|---|---|---|
| CENSTAR-7-DIGIT | `CEN7` | 2 | 19 × 12-bit words (digit + index), clock idles low, 44 ms period | ✔ 1.750 L × 423.00 = 740.25 decoded exactly |
| CENSTAR-7-CS | `C7CS` | 3 | same words in reverse index order + fixed tail (≥ 304 bits), clock idles high, 61 ms | ✔ 1.625 L × 423.00 = 687.37 decoded exactly |
| WAYNE-6-DIGIT | `WAY6` | 5 | fixed 51-byte timed sequence, 7-segment, price/total 0.1, volume 0.01, 95 ms | ✔ 1.56 L × 423.0 = 659.8 decoded (needs the COM-firmware Wayne scaling fix, below) |
| HONGYANG-8-DIGIT | `HY8` | 4 | 14 byte pairs on SDATA1 + SDATA2 (type 2 timing), start/stop digit = nozzle, 40 ms | ✔ 1.625 L × 423.00 = 687.37 decoded exactly |
| WAYNE-6-DIGIT-2 | `WAY2` | 8 | built from the DT decoder (no simulator): SPI mode 0, FF FF sync at +40 ms, then volume (0.001), total (0.1), 0x20, 0x10 + rate (0.1, own segment table) at +60 ms, one cycle per 200 ms; default price 1.00 | ✔ 1.625 L × 1.00 = 1.6 and 1.75 L × 313.0 = 547.7 decoded (ESP32 DT only — the ESP07 DT has no type 8) |

Defaults: price 423.00 (`PRICE` to change), step 125 mL per 100 ms (Wayne 120 mL, as its volume has 0.01 L resolution).

**Wayne:** the DT (ESP07 and ESP32) sends Wayne values as raw display counts (price 4230 for 423.0, volume
156 for 1.56 L, total 6598 for 659.8). The COM firmware used to take them as ×100 / ×1000 values and rejected
every frame; `fuel_types_from_frame()` (`src/app-modules/module_fuel/pumps/fuel_types.h`) now scales Wayne-6
by 10 for all three, as the old ferp-com firmware did. Totals show with one decimal on a Wayne, so the
decoded total is the display value (659.8), not the emulator's exact 659.88.

**Switching the device's type:** set `DISPLAY_TYPE` (0x6001) and reboot it. The device logs
`distap_set_display_type(n) failed` at boot, but the DT still takes the new type; occasionally a second
reboot is needed before frames are decoded.
