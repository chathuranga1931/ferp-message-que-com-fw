#include "pumps_extra.h"
#include "board_mega.h"
#include "control.h"

// ── shared ──────────────────────────────────────────────────────────────────

static const uint8_t BLANK = 0xF;      // BCD-nibble protocols: blank digit

// Nozzle / pumping (switch, button or PC), volume ramp every 100 ms, LEDs and nozzle outputs.
static void pump_common() {
    bool nozzle = control_nozzle(digitalRead(c_pin_btn_nzl_status) == LOW);
    bool pumping = control_pumping(digitalRead(c_pin_btn_pumping) == LOW);
    digitalWrite(c_pin_nozzle1_io_port, nozzle);
    digitalWrite(c_pin_nozzle2_io_port, nozzle);

    static bool nozzle_prev = false;
    if (nozzle != nozzle_prev) {
        if (nozzle) { emu.volume_ml = 0; control_event("ZERO"); }
        nozzle_prev = nozzle;
    }
    if (nozzle && pumping) {
        static uint32_t ts = 0;
        if (millis() - ts > 100) { ts = millis(); emu.volume_ml += emu.step_ml; }
        static bool g = false;
        digitalWrite(c_pin_green_led, g);
        g = !g;
    } else {
        digitalWrite(c_pin_green_led, LOW);
    }
    digitalWrite(c_pin_red_led, nozzle);
    emu.dirty = false;                   // frames are rebuilt from the current values every time
}

static void set_defaults(ctl_mode_t mode, uint32_t price, uint32_t step) {
    emu.mode = mode;
    if (!emu.price_set) emu.unit_price = price;
    if (!emu.step_set) emu.step_ml = step;
}

// value -> n digits, least significant first; leading zeros blank, digit 0 always shown
static void bcd_digits(uint32_t v, uint8_t *out, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) {
        if (v) { out[i] = v % 10; v /= 10; }
        else out[i] = (i == 0) ? 0 : BLANK;
    }
}

static inline void clk(bool level) { if (level) CLOCK_1_HIGH; else CLOCK_1_LOW; }
static inline void d1(bool level) { DATA_1_WRITE(level ? 1 : 0); }
static inline void d2(bool level) { DATA_2_WRITE(level ? 1 : 0); }
static inline void rclk(bool level) { CS_1_WRITE(level ? 1 : 0); }

static bool frame_due(uint16_t gap_ms) {
    static uint32_t last = 0;
    if (millis() - last < gap_ms) return false;
    last = millis();
    return true;
}

// ── CENSTAR-7 and CENSTAR-7-CS ──────────────────────────────────────────────
// 19 words of 12 bits (DIGIT nibble, then INDEX byte), MSB first.

static bool c7_cs = false;
static const uint8_t C7_IDX[19] = { 0x05, 0x15, 0x25, 0x35, 0x45, 0x55, 0x65, 0x75, 0x85, 0x95, 0xA5, 0xB5,
                                    0xC5, 0xD5, 0xE5, 0xF3, 0xF0, 0xF1, 0xF2 };
static const uint8_t C7CS_IDX[19] = { 0x45, 0x35, 0x25, 0x15, 0x05, 0xB5, 0xA5, 0x95, 0x85, 0x75, 0x65, 0x55,
                                      0xF2, 0xF1, 0xF0, 0xF3, 0xE5, 0xD5, 0xC5 };
// CS variant: fixed tail after the 19 words (no RCLK) — the decoder needs >= 304 bits per burst
static const uint16_t C7CS_TAIL[23] = { 0x045, 0x01A, 0xC4C, 0xBE2, 0xBE0, 0xA1B, 0x506, 0x942, 0x5BF, 0x85F, 0xBD7,
                                        0xD9B, 0xEAA, 0x1E4, 0x0F1, 0x0F0, 0x0F3, 0xFE6, 0xFEA, 0xFE1, 0x40B, 0xFEC, 0x64F };

void censtar7_init(bool cs_variant) {
    c7_cs = cs_variant;
    set_defaults(cs_variant ? CTL_C7CS : CTL_CEN7, 42300, 125);
    d1(0); d2(0);
    clk(cs_variant);                      // plain: clock idles LOW, CS: idles HIGH
    rclk(0);
}

// plain Censtar-7: clock idles low, data valid on the rising edge (simulator send_12bits timing)
static void c7_word(uint16_t w) {
    for (int8_t b = 11; b >= 0; b--) {
        d1((w >> b) & 1);
        delayMicroseconds(1);
        clk(1);
        delayMicroseconds(b >= 8 ? 2 : 2);   // >= 2 us high so the DT edge interrupt sees it
        clk(0);
        delayMicroseconds(b == 0 ? 5 : 9);
        if (b == 8) delayMicroseconds(9);    // extra gap after the digit nibble
    }
    rclk(1); delayMicroseconds(2); rclk(0); delayMicroseconds(37);
}

// CS variant: clock idles high, data set while high, sampled on the rising edge (SPI mode 3)
static void c7cs_bits(uint32_t w, uint8_t n) {
    for (int8_t b = n - 1; b >= 0; b--) {
        d1((w >> b) & 1);
        clk(0);
        delayMicroseconds(2);
        clk(1);
        delayMicroseconds(2);
    }
}
static void c7cs_latch() { rclk(1); delayMicroseconds(2); rclk(0); delayMicroseconds(1); }

void censtar7_run() {
    pump_common();
    if (!frame_due(c7_cs ? 61 : 44)) return;     // simulator: frame + 59 / 40 ms gap

    uint8_t price[5], total[7], vol[7];
    bcd_digits(emu.unit_price, price, 5);
    bcd_digits(control_total(), total, 7);
    bcd_digits(emu.volume_ml, vol, 7);
    uint8_t dig[19];
    for (uint8_t i = 0; i < 5; i++) dig[i] = price[i];
    for (uint8_t i = 0; i < 7; i++) dig[5 + i] = total[i];
    for (uint8_t i = 0; i < 7; i++) dig[12 + i] = vol[i];

    if (!c7_cs) {
        for (uint8_t k = 0; k < 19; k++) c7_word(((uint16_t)dig[k] << 8) | C7_IDX[k]);
    } else {
        for (uint8_t k = 0; k < 19; k++) { c7cs_bits(((uint16_t)dig[k] << 8) | C7CS_IDX[k], 12); c7cs_latch(); }
        c7cs_bits(0x2FF6, 16); c7cs_latch();
        c7cs_bits(0x24FF6UL, 20); c7cs_latch();
        for (uint8_t k = 0; k < 23; k++) c7cs_bits(C7CS_TAIL[k], 12);
        clk(1);
    }
}

// ── WAYNE-6 ─────────────────────────────────────────────────────────────────
// Fixed timed sequence (wayne_6_digit simulator); bytes MSB first, clock idles HIGH, data on the
// rising edge; the DT shifts bits in only while RCLK is LOW.  7-segment digits with decimal points.

enum : uint8_t { W_DELAY = 0, W_RCLK, W_DATA };
struct w_op_t { uint8_t mode; uint16_t data; };

// rows 6..17 = volume 5..0, total 5..0 ; rows 25..28 = unit 3..0 (filled before each frame)
static w_op_t wseq[] = {
    {W_RCLK, 1}, {W_DELAY, 500}, {W_DATA, 0x12}, {W_DELAY, 290}, {W_RCLK, 0}, {W_DELAY, 65},
    {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0},
    {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0},
    {W_DELAY, 280}, {W_RCLK, 1}, {W_DELAY, 60}, {W_DATA, 0x13}, {W_DELAY, 275}, {W_RCLK, 0}, {W_DELAY, 25},
    {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0}, {W_DATA, 0},
    {W_DELAY, 310}, {W_RCLK, 1}, {W_DELAY, 450}, {W_DATA, 0xF0}, {W_DELAY, 400}, {W_RCLK, 0}, {W_DELAY, 20},
    {W_DATA, 0xFE}, {W_DELAY, 310}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 2},
    {W_DATA, 0xFE}, {W_DELAY, 340}, {W_RCLK, 1}, {W_DELAY, 0}, {W_RCLK, 0}, {W_DELAY, 1},
    {W_DATA, 0xFE}, {W_DELAY, 280}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 1},
    {W_DATA, 0xFE}, {W_DELAY, 350}, {W_RCLK, 1}, {W_DELAY, 0}, {W_RCLK, 0}, {W_DELAY, 1},
    {W_DATA, 0xFD}, {W_DELAY, 250}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 1},
    {W_DATA, 0xFD}, {W_DELAY, 350}, {W_RCLK, 1}, {W_DELAY, 0}, {W_RCLK, 0}, {W_DELAY, 1},
    {W_DATA, 0xFB}, {W_DELAY, 300}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 1},
    {W_DATA, 0xFB}, {W_DELAY, 355}, {W_RCLK, 1}, {W_DELAY, 355}, {W_DATA, 0x22}, {W_DELAY, 290}, {W_RCLK, 0}, {W_DELAY, 20},
    {W_DATA, 0x00}, {W_DATA, 0x00}, {W_DATA, 0x00}, {W_DATA, 0x00}, {W_DATA, 0x00}, {W_DATA, 0x01}, {W_DATA, 0xFB}, {W_DATA, 0xF9},
    {W_DELAY, 240}, {W_RCLK, 1}, {W_DELAY, 240}, {W_DATA, 0x11}, {W_DELAY, 410}, {W_RCLK, 0}, {W_DELAY, 9},
    {W_DATA, 0xC0}, {W_DELAY, 260}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 5},
    {W_DATA, 0xC0}, {W_DELAY, 410}, {W_RCLK, 1}, {W_DELAY, 0}, {W_RCLK, 0}, {W_DELAY, 5},
    {W_DATA, 0xA0}, {W_DELAY, 325}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 5},
    {W_DATA, 0xA0}, {W_DELAY, 335}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 5},
    {W_DATA, 0x60}, {W_DELAY, 345}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 5},
    {W_DATA, 0x60}, {W_DELAY, 415}, {W_RCLK, 1}, {W_DELAY, 0}, {W_RCLK, 0}, {W_DELAY, 5},
    {W_DATA, 0xE0}, {W_DELAY, 440}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 18},
    {W_DATA, 0xE0}, {W_DELAY, 280}, {W_RCLK, 1}, {W_DELAY, 250},
    {W_DATA, 0x21}, {W_DELAY, 280}, {W_RCLK, 0}, {W_DELAY, 18},
    {W_DATA, 0x01}, {W_DELAY, 320}, {W_RCLK, 1}, {W_DELAY, 0}, {W_RCLK, 0}, {W_DELAY, 2},
    {W_DATA, 0x02}, {W_DELAY, 400}, {W_RCLK, 1}, {W_DELAY, 0}, {W_RCLK, 0}, {W_DELAY, 2},
    {W_DATA, 0x04}, {W_DELAY, 250}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 1},
    {W_DATA, 0x08}, {W_DELAY, 370}, {W_RCLK, 1}, {W_DELAY, 1}, {W_RCLK, 0}, {W_DELAY, 1},
    {W_DATA, 0x10}, {W_DELAY, 220}, {W_RCLK, 1},
};
static const uint8_t W_IDX_VOL = 6, W_IDX_TOT = 12, W_IDX_UNIT = 25;
static const uint8_t SEG[10] = { 0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xF6 };   // decimal point = | 0x01

// value -> n 7-segment codes, most significant first; leading zeros blank (0x00); dot_pos counted from the right
static void seg_fill(uint8_t row, uint32_t v, uint8_t n, uint8_t dot_pos) {
    for (int8_t i = n - 1; i >= 0; i--) {             // i = position from the left
        uint8_t from_right = (n - 1) - i;
        if (v) { uint8_t c = SEG[v % 10]; if (from_right == dot_pos) c |= 0x01; wseq[row + i].data = c; v /= 10; }
        else wseq[row + i].data = 0x00;
    }
}

static void w_byte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        d1(b & 0x80); b <<= 1;
        clk(0); delayMicroseconds(10);
        clk(1); delayMicroseconds(10);
    }
    d1(0);
}

void wayne6_init() {
    set_defaults(CTL_WAY6, 42300, 120);               // volume shown in 0.01 L: keep the step a multiple of 10 mL
    d1(0); clk(1); rclk(1);
}

void wayne6_run() {
    pump_common();
    if (!frame_due(95)) return;                    // simulator: frame (~12 ms) + 82 ms gap
    uint32_t unit_x10 = emu.unit_price / 10;          // display: price 0.1, total 0.1, volume 0.01
    uint32_t vol_x100 = emu.volume_ml / 10;
    uint32_t total_x10 = (uint32_t)((uint64_t)vol_x100 * unit_x10 / 100);
    seg_fill(W_IDX_VOL, vol_x100, 6, 2);
    seg_fill(W_IDX_TOT, total_x10, 6, 1);
    seg_fill(W_IDX_UNIT, unit_x10, 4, 1);
    for (uint16_t k = 0; k < sizeof(wseq) / sizeof(wseq[0]); k++) {
        switch (wseq[k].mode) {
            case W_DELAY: if (wseq[k].data) delayMicroseconds(wseq[k].data); break;
            case W_RCLK: rclk(wseq[k].data); break;
            default: w_byte((uint8_t)wseq[k].data); break;
        }
    }
}

// ── HONGYANG-8 (type 2) ─────────────────────────────────────────────────────
// 14 byte pairs on SDATA1 + SDATA2 (shared clock), MSB first, clock idles LOW, data on the rising edge.
// Byte = digit << 4 | index.  SDATA1: [0..7] total (MSD first), [8..13] unit price.
// SDATA2: [0] select-P (blank), [1..7] volume, [8] start/stop (1 = nozzle up), [9..13] fixed.

struct hy_row_t { uint8_t d1, d2, bit_idx, bit_delay; uint16_t rclk_high; };
static hy_row_t hy[14] = {
    {0xF0, 0xF0, 8, 0, 14}, {0xF1, 0xF1, 8, 0, 14}, {0xF2, 0xF2, 8, 0, 14}, {0x53, 0xF3, 3, 57, 14},
    {0x84, 0x14, 8, 0, 14}, {0x95, 0x95, 8, 0, 14}, {0x56, 0x36, 6, 57, 14}, {0x67, 0x37, 8, 0, 6436},
    {0xF8, 0x18, 5, 59, 14}, {0x39, 0xA9, 8, 0, 14}, {0x0A, 0x1A, 8, 0, 14}, {0x5B, 0xAB, 8, 0, 105},
    {0x0C, 0xFC, 8, 0, 14}, {0x0D, 0xAD, 8, 0, 634},
};

void hongyang8_init() {
    set_defaults(CTL_HY8, 42300, 125);
    d1(0); d2(0); clk(0); rclk(0);
}

static void hy_byte(const hy_row_t &r) {
    for (uint8_t i = 0; i < 8; i++) {
        if (i) delayMicroseconds(14);
        d1(r.d1 & (0x80 >> i));
        d2(r.d2 & (0x80 >> i));
        delayMicroseconds(r.bit_idx == i ? r.bit_delay : 3);
        clk(1); delayMicroseconds(2); clk(0);
    }
    delayMicroseconds(7);
    d1(0); d2(0);
    rclk(1); delayMicroseconds(r.rclk_high); rclk(0);
    delayMicroseconds(59);
}

void hongyang8_run() {
    pump_common();
    if (!frame_due(40)) return;                    // simulator: frame (~10 ms) + 29 ms gap
    uint8_t total[8], unit[6], vol[7];
    bcd_digits(control_total(), total, 8);
    bcd_digits(emu.unit_price, unit, 6);
    bcd_digits(emu.volume_ml, vol, 7);
    for (uint8_t i = 0; i < 8; i++) hy[i].d1 = (total[7 - i] << 4) | i;          // [0] = MSD
    for (uint8_t i = 0; i < 6; i++) hy[8 + i].d1 = (unit[5 - i] << 4) | (8 + i);
    hy[0].d2 = (BLANK << 4) | 0;                                                   // select-P: blank
    for (uint8_t i = 0; i < 7; i++) hy[1 + i].d2 = (vol[6 - i] << 4) | (1 + i);
    // start/stop digit: Hongyang reports the nozzle in the frame itself (1 = nozzle up, blank = down)
    hy[8].d2 = ((emu.nozzle ? 1 : BLANK) << 4) | 8;
    for (uint8_t k = 0; k < 14; k++) hy_byte(hy[k]);
}

// ── WAYNE-6 type 2 (device display type 8) ──────────────────────────────────
// Built from the DT decoder (distap-esp32/components/wayn2/wayn_6_digit_2.c) — there is no simulator.
// SPI mode 0 (clock idles LOW, data valid on the rising edge), MSB first, on SCLK (pin 2) / SDATA1 (pin 3).
// The DT waits for 45 ms of RCLK silence, then synthesises CS and captures for a fixed window; on the real
// pump that capture starts 2 bytes into the frame (after the FF FF sync), which the decoder relies on.
// So each cycle: one RCLK pulse, sync at +40 ms (before the capture opens), the data at +60 ms (after it).
// Frame as captured: volume 6 digits (0.001, LSB first), total 6 (0.1), 0x20, 0x10, rate 4 digits (0.1),
// 2 more payload bytes the decoder does not use (sent as 0x00).

static const uint8_t W2_MAIN[10] = { 0xFC, 0x60, 0xDA, 0xF2, 0x66, 0xB6, 0xBE, 0xE0, 0xFE, 0xE6 };  // dp = bit0
static const uint8_t W2_RATE[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };  // dp = bit7
static uint8_t w2_phase = 0;                 // 0 = send RCLK pulse, 1 = sync due, 2 = data due
static uint32_t w2_t0 = 0;

void wayne2_init() {
    set_defaults(CTL_WAY2, 100, 125);        // the pump in the field runs at a fixed 1.00 unit price
    d1(0); clk(0); rclk(0);
    w2_phase = 0;
}

static void w2_byte(uint8_t b) {
    for (uint8_t i = 0; i < 8; i++) {
        d1(b & 0x80); b <<= 1;
        delayMicroseconds(2);
        clk(1); delayMicroseconds(2);
        clk(0); delayMicroseconds(2);
    }
}

// value -> n digits, least significant first, with the decimal point on digit dp_pos
static void w2_digits(uint32_t v, uint8_t *out, uint8_t n, uint8_t dp_pos, const uint8_t *table, uint8_t dp_bit) {
    for (uint8_t i = 0; i < n; i++) {
        uint8_t c = table[v % 10];
        v /= 10;
        if (i == dp_pos) c |= dp_bit;
        out[i] = c;
    }
}

void wayne2_run() {
    pump_common();
    uint32_t now = millis();
    if (w2_phase == 0) {
        if (now - w2_t0 < 200) return;                 // one capture cycle every 200 ms
        w2_t0 = now;
        rclk(1); delayMicroseconds(5); rclk(0);        // falling edge: the DT starts its 45 ms quiet timer
        w2_phase = 1;
    } else if (w2_phase == 1) {
        if (now - w2_t0 < 40) return;
        w2_byte(0xFF); w2_byte(0xFF);                  // sync, before the DT opens its capture
        w2_phase = 2;
    } else {
        if (now - w2_t0 < 60) return;
        uint32_t rate_x10 = emu.unit_price / 10;       // display: rate 0.1, total 0.1, volume 0.001
        uint32_t total_x10 = (uint32_t)((uint64_t)emu.volume_ml * rate_x10 / 1000);
        uint8_t vol[6], tot[6], rate[4];
        w2_digits(emu.volume_ml, vol, 6, 3, W2_MAIN, 0x01);
        w2_digits(total_x10, tot, 6, 1, W2_MAIN, 0x01);
        w2_digits(rate_x10, rate, 4, 1, W2_RATE, 0x80);
        for (uint8_t i = 0; i < 6; i++) w2_byte(vol[i]);
        for (uint8_t i = 0; i < 6; i++) w2_byte(tot[i]);
        w2_byte(0x20);
        w2_byte(0x10);
        for (uint8_t i = 0; i < 4; i++) w2_byte(rate[i]);
        w2_byte(0x00); w2_byte(0x00);
        w2_phase = 0;
    }
}
