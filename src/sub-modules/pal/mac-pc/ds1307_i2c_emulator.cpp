// ds1307_i2c_emulator.cpp
//
// macOS I2C emulator for the DS1307 real-time clock.
//
// DS1307 register map (7 bytes read by ds1307_read_time):
//   [0] Seconds  — bits 6:0 BCD, bit 7 = CH (Clock Halt, 0 = running)
//   [1] Minutes  — bits 6:0 BCD
//   [2] Hours    — bits 5:0 BCD (24-h mode, bit 6 = 0)
//   [3] Day      — bits 2:0 BCD  (1 = Sunday, 7 = Saturday)
//   [4] Date     — bits 5:0 BCD
//   [5] Month    — bits 4:0 BCD
//   [6] Year     — bits 7:0 BCD  (00–99, relative to 2000)
//   [7] Control  — not decoded by driver; emulator returns 0
//
// Write format (8 bytes sent by ds1307_set_time / ds1307_init CH clear):
//   [0] Register start address (0x00)
//   [1..7] same layout as above

#include "ds1307_i2c_emulator.h"

#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

typedef struct {
    int64_t time_offset_s;  /**< Added to gettimeofday() to simulate set-time */
} ds1307_ctx_t;

static ds1307_ctx_t s_ctx;

// ---------------------------------------------------------------------------
// BCD helpers
// ---------------------------------------------------------------------------

static inline uint8_t dec_to_bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4u) | (v % 10u)); }
static inline uint8_t bcd_to_dec(uint8_t v) { return (uint8_t)((v >> 4u) * 10u + (v & 0x0Fu)); }

// ---------------------------------------------------------------------------
// Populate register bank from a time_t value
// ---------------------------------------------------------------------------

static void populate_regs(uint8_t regs[8], time_t t)
{
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    regs[0] = dec_to_bcd((uint8_t)tm_info.tm_sec)  & 0x7Fu;  /* CH bit = 0 (clock running) */
    regs[1] = dec_to_bcd((uint8_t)tm_info.tm_min);
    regs[2] = dec_to_bcd((uint8_t)tm_info.tm_hour) & 0x3Fu;  /* 24-h mode */
    regs[3] = dec_to_bcd((uint8_t)(tm_info.tm_wday + 1));    /* 1–7 */
    regs[4] = dec_to_bcd((uint8_t)tm_info.tm_mday);
    regs[5] = dec_to_bcd((uint8_t)(tm_info.tm_mon + 1));     /* 1–12 */
    regs[6] = dec_to_bcd((uint8_t)(tm_info.tm_year % 100));  /* 00–99 */
    regs[7] = 0x00u;                                           /* control register */
}

// ---------------------------------------------------------------------------
// Emulator callbacks
// ---------------------------------------------------------------------------

/**
 * on_write_read — handles pal_i2c_write_read().
 *
 * The DS1307 driver always writes one byte (register pointer = 0x00) then
 * reads 1 or 7 bytes from the register bank.
 */
static int32_t ds1307_on_write_read(const uint8_t *wr, size_t wr_len,
                                     uint8_t *rd,       size_t rd_len,
                                     void    *ctx)
{
    if (!wr || wr_len < 1u || !rd || rd_len == 0u) return -1;

    const uint8_t reg_start = wr[0];

    struct timeval tv;
    gettimeofday(&tv, NULL);
    const time_t t = tv.tv_sec + ((ds1307_ctx_t *)ctx)->time_offset_s;

    uint8_t regs[8];
    populate_regs(regs, t);

    size_t count = rd_len;
    if ((size_t)reg_start + count > sizeof(regs)) {
        count = sizeof(regs) - (size_t)reg_start;
    }
    memcpy(rd, regs + reg_start, count);
    return 0;  /* PAL_OK */
}

/**
 * on_write — handles pal_i2c_write().
 *
 * Two cases:
 *   len == 2, data[0] == 0x00: ds1307_init clears the CH bit — ignored
 *             (we never set CH, so the clock is already "running").
 *   len == 8, data[0] == 0x00: ds1307_set_time writes 7 BCD time bytes —
 *             decode and store a time offset vs. gettimeofday().
 */
static int32_t ds1307_on_write(const uint8_t *data, size_t len, void *ctx)
{
    if (!data || len < 2u || data[0] != 0x00u) return 0;  /* ignore unknown reg writes */

    if (len < 8u) {
        /* CH-bit clear or short write — nothing to do */
        return 0;
    }

    /* Decode 7 BCD bytes into struct tm */
    struct tm tm_info;
    memset(&tm_info, 0, sizeof(tm_info));
    tm_info.tm_sec  = (int)bcd_to_dec(data[1] & 0x7Fu);
    tm_info.tm_min  = (int)bcd_to_dec(data[2]);
    tm_info.tm_hour = (int)bcd_to_dec(data[3] & 0x3Fu);
    /* data[4] = wday (1-7) — not needed for mktime */
    tm_info.tm_mday = (int)bcd_to_dec(data[5]);
    tm_info.tm_mon  = (int)bcd_to_dec(data[6]) - 1;       /* 0-based month */
    tm_info.tm_year = (int)bcd_to_dec(data[7]) + 100;     /* years since 1900 */
    tm_info.tm_isdst = -1;

    const time_t set_time = mktime(&tm_info);
    if (set_time == (time_t)-1) return 0;  /* invalid time — ignore */

    struct timeval tv_now;
    gettimeofday(&tv_now, NULL);
    ((ds1307_ctx_t *)ctx)->time_offset_s = (int64_t)set_time - (int64_t)tv_now.tv_sec;
    return 0;  /* PAL_OK */
}

// ---------------------------------------------------------------------------
// Static emulator instance
// ---------------------------------------------------------------------------

static pal_i2c_emulator_t s_emulator = {
    .on_write      = ds1307_on_write,
    .on_write_read = ds1307_on_write_read,
    .on_read       = NULL,
    .ctx           = &s_ctx,
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ds1307_i2c_emulator_init(void)
{
    s_ctx.time_offset_s = 0;
}

pal_i2c_emulator_t *ds1307_i2c_emulator_get(void)
{
    return &s_emulator;
}
