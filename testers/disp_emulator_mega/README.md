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
| Rotary encoder with push button | KY-040 or equivalent |
| 2× LEDs (red / green) | With current-limiting resistors |
| 2× push buttons (tactile) | Nozzle status + Pumping |
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
| 17 | ENCODER\_BTN | Input | — | — | Confirm selection; active LOW, INPUT\_PULLUP |
| 18 | ENCODER\_A | Input | — | — | Rotary encoder channel A; hardware interrupt |
| 19 | ENCODER\_B | Input | — | — | Rotary encoder channel B; hardware interrupt |
| 46 | GREEN\_LED | Output | — | — | Blinks while pumping active |
| 48 | RED\_LED | Output | — | — | ON while nozzle is up |
| 50 | BTN\_PUMPING | Input | — | — | Hold LOW to simulate active pumping; INPUT\_PULLUP |
| 52 | BTN\_NZL\_STATUS | Input | — | — | Hold LOW to simulate nozzle lifted; INPUT\_PULLUP |

---

## Startup — Configuration Phase

1. The OLED shows **"Configuring… / Display Mode:"** and the currently selected type.
2. Turn the **rotary encoder** to scroll through the supported display types:
   - `SANKI-6-DIGIT`
   - `CENSTAR-6-DIGIT`
   - `CENSTAR-7-DIGIT` *(not implemented — WDT reset if selected)*
   - `WAYNE-6-DIGIT` *(not implemented — WDT reset if selected)*
   - `HONGYANG-8-DIGIT` *(not implemented — WDT reset if selected)*
3. **Press the encoder button** to confirm. The selected protocol driver is initialised
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

## Rotary Encoder

- **Pins:** A = 18, B = 19 (hardware interrupt pins on the Mega).
- Both edges trigger interrupts; a **50 ms debounce** is applied per channel.
- The quadrature state machine increments or decrements `encoderPosition` based on
  the 4-bit state transition table.
- In the configuration menu, each step of the encoder moves the menu cursor by ±1,
  clamped to `[0, pump_type_count − 1]`.

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
