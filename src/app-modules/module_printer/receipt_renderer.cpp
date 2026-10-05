// receipt_renderer.cpp
//
// Port of the legacy printer.cpp layouts — see receipt_renderer.h.
// Keep the byte sequences and line texts identical to the legacy firmware
// so printed receipts look the same after the upgrade.

#include "receipt_renderer.h"

#include <stdio.h>
#include <string.h>

// ── ESC/POS command bytes (names as in the legacy firmware) ─────────────────
static const uint8_t k_init[]           = {0x1b, 0x40};
static const uint8_t k_unknown2[]       = {0x1b, 0x52, 0x00};             // intl. char set: USA
static const uint8_t k_unknown3[]       = {0x1b, 0x63, 0x33, 0x04, 0x0d}; // paper sensor (roll end) + CR
static const uint8_t k_roll_end[]       = {0x1b, 0x63, 0x33, 0x04};
static const uint8_t k_codepage0[]      = {0x1b, 0x74, 0x00};
static const uint8_t k_size_normal[]    = {0x1d, 0x21, 0x00};
static const uint8_t k_font_b[]         = {0x1b, 0x21, 0x01};
static const uint8_t k_font_a[]         = {0x1b, 0x21, 0x00};
static const uint8_t k_font_b_dh[]      = {0x1b, 0x21, 0x11};
static const uint8_t k_font_a_dh[]      = {0x1b, 0x21, 0x10};
static const uint8_t k_cut[]            = {0x1b, 0x69};

static const char *k_default_footer = "www.myfuelstation.net (0712209310)";

static inline const char *_s(const char *p) { return p ? p : ""; }

// ── Date helpers ────────────────────────────────────────────────────────────

typedef struct { int year, month, day, hour, minute, second; } dt_t;

static bool _parse_dt(const char *s, dt_t *dt)
{
    memset(dt, 0, sizeof(*dt));
    if (!s || strlen(s) < 19) return false;
    int n = sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d",
                   &dt->year, &dt->month, &dt->day, &dt->hour, &dt->minute, &dt->second);
    if (n != 6) { memset(dt, 0, sizeof(*dt)); return false; }
    return true;
}

/** "2025-01-01T01:23:45" → "01/01/2025  01:23:45" (legacy getPrintableTime). */
static void _printable_time(const char *iso, char *out, size_t out_len)
{
    dt_t dt;
    if (!_parse_dt(iso, &dt)) { snprintf(out, out_len, "Invalid Time"); return; }
    snprintf(out, out_len, "%02d/%02d/%04d  %02d:%02d:%02d",
             dt.day, dt.month, dt.year, dt.hour, dt.minute, dt.second);
}

// ── Low-level output ────────────────────────────────────────────────────────

void ReceiptRenderer::_raw(const void *data, size_t n)
{
    if (_overflow) return;
    if (_len + n > _cap) { _overflow = true; return; }
    memcpy(_buf + _len, data, n);
    _len += n;
}

void ReceiptRenderer::_str(const char *s) { _raw(s, strlen(s)); }

// ── Line builders ───────────────────────────────────────────────────────────

/** left + spaces + right, padded to _width (left truncated when too long). */
void ReceiptRenderer::_right(const char *left, const char *right)
{
    left = _s(left); right = _s(right);
    size_t ll = strlen(left), rl = strlen(right);
    int32_t spaces = (int32_t)_width - (int32_t)ll - (int32_t)rl;
    size_t pos = 0;
    const size_t max = LINE_MAX - 2;      // room for '\n' + NUL

    auto put = [&](char c) { if (pos < max) _line[pos++] = c; };

    if (spaces < 0) {
        int32_t keep = (int32_t)_width - (int32_t)rl;
        if (keep < 0) keep = 0;
        for (int32_t i = 0; i < keep && left[i]; i++) put(left[i]);
    } else {
        for (size_t i = 0; i < ll; i++) put(left[i]);
        for (int32_t i = 0; i < spaces; i++) put(' ');
    }
    for (size_t i = 0; i < rl; i++) put(right[i]);
    _line[pos++] = '\n';
    _line[pos]   = '\0';
    _str(_line);
}

/** Centre text in _width (truncated to _width when longer). */
void ReceiptRenderer::_center(const char *text)
{
    text = _s(text);
    size_t tl = strlen(text);
    int32_t spaces = (int32_t)_width - (int32_t)tl;
    size_t pos = 0;
    const size_t max = LINE_MAX - 2;

    if (spaces <= 0) {
        for (uint32_t i = 0; i < _width && text[i] && pos < max; i++) _line[pos++] = text[i];
    } else {
        int32_t pad = (spaces + 1) / 2;
        for (int32_t i = 0; i < pad && pos < max; i++) _line[pos++] = ' ';
        // legacy: strncat(out, add, WIDTH - strlen(out))
        size_t room = (_width > pos) ? _width - pos : 0;
        for (size_t i = 0; i < tl && i < room && pos < max; i++) _line[pos++] = text[i];
    }
    _line[pos++] = '\n';
    _line[pos]   = '\0';
    _str(_line);
}

/** prefix followed by c up to _width. */
void ReceiptRenderer::_fill(const char *prefix, char c)
{
    prefix = _s(prefix);
    size_t pos = 0;
    const size_t max = LINE_MAX - 2;
    for (size_t i = 0; prefix[i] && pos < max; i++) _line[pos++] = prefix[i];
    while (pos < _width && pos < max) _line[pos++] = c;
    _line[pos++] = '\n';
    _line[pos]   = '\0';
    _str(_line);
}

void ReceiptRenderer::_blank_lines(uint32_t n, uint32_t cap)
{
    if (n > cap) n = cap;
    for (uint32_t i = 0; i < n; i++) _str("\n");
}

// ── Building blocks ─────────────────────────────────────────────────────────

void ReceiptRenderer::_cmd_body_reset()
{
    _raw(k_init, sizeof(k_init));
    _raw(k_unknown2, sizeof(k_unknown2));
    _raw(k_unknown3, sizeof(k_unknown3));
    _raw(k_size_normal, sizeof(k_size_normal));
    _raw(k_font_b, sizeof(k_font_b));
}

void ReceiptRenderer::_header_theme1(const receipt_cfg_t &cfg)
{
    _cmd_body_reset();
    _raw(k_font_a_dh, sizeof(k_font_a_dh));
    if (_s(cfg.name_l1)[0]) _center(cfg.name_l1);
    _raw(k_font_a, sizeof(k_font_a));
    if (_s(cfg.name_l2)[0]) _center(cfg.name_l2);
    if (_s(cfg.add_l1)[0])  _center(cfg.add_l1);
    if (_s(cfg.add_l2)[0])  _center(cfg.add_l2);
    if (_s(cfg.tele)[0])    _center(cfg.tele);
}

void ReceiptRenderer::_header_theme2(const receipt_cfg_t &cfg)
{
    _cmd_body_reset();
    _raw(k_font_b_dh, sizeof(k_font_b_dh));
    if (_s(cfg.name_l1)[0]) _center(cfg.name_l1);
    _raw(k_font_b, sizeof(k_font_b));
    if (_s(cfg.name_l2)[0]) _center(cfg.name_l2);
    if (_s(cfg.add_l1)[0])  _center(cfg.add_l1);
    if (_s(cfg.add_l2)[0])  _center(cfg.add_l2);
    if (_s(cfg.tele)[0])    _center(cfg.tele);
}

void ReceiptRenderer::_header_theme3(const receipt_cfg_t &cfg)
{
    _raw(k_init, sizeof(k_init));
    _raw(k_unknown2, sizeof(k_unknown2));
    _raw(k_unknown3, sizeof(k_unknown3));
    _raw(k_size_normal, sizeof(k_size_normal));
    _raw(k_font_b_dh, sizeof(k_font_b_dh));
    if (_s(cfg.name_l1)[0]) _right(cfg.name_l1, "");
    if (_s(cfg.name_l2)[0]) _right(cfg.name_l2, "");
    _raw(k_font_b, sizeof(k_font_b));
    if (_s(cfg.add_l1)[0])  _center(cfg.add_l1);
    if (_s(cfg.add_l2)[0])  _center(cfg.add_l2);
    if (_s(cfg.tele)[0])    _center(cfg.tele);
}

void ReceiptRenderer::_thank_you(const receipt_cfg_t &cfg, int lines)
{
    for (int i = 0; i < lines && i < 6; i++) {
        if (_s(cfg.thank[i])[0]) _center(cfg.thank[i]);
    }
}

void ReceiptRenderer::_tail(const receipt_cfg_t &cfg)
{
    _center(_s(cfg.footer)[0] ? cfg.footer : k_default_footer);
    _blank_lines(cfg.end_lines, 15);
    if (cfg.en_cut) {
        _raw(k_cut, sizeof(k_cut));
        _blank_lines(cfg.lines_after_cut, 15);
    }
}

/** "Rs 700.95" → "---------" / "=========" of the same length. */
static void _rule(char *out, size_t out_len, double total, char c)
{
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "Rs %0.2f", total);
    size_t n = strlen(tmp);
    if (n >= out_len) n = out_len - 1;
    memset(out, c, n);
    out[n] = '\0';
}

// ── Themes ──────────────────────────────────────────────────────────────────

void ReceiptRenderer::_theme1(const receipt_cfg_t &cfg, const receipt_job_t &job, const char *bill)
{
    char buf[64];
    _header_theme1(cfg);
    _cmd_body_reset();
    _raw(k_font_a, sizeof(k_font_a));

    if (cfg.en_print_time) _right("Bill Issued : ", job.print_time);
    _right("Bill Number : ", bill);
    _center(job.print_note);
    _fill("", '-');
    _fill("Vehicle Nu : ", '.');
    _right("Fueled Time : ", job.time_stamp);
    _right("Pump ID : ", job.nozzle_id);
    _right("Fuel Catogory : ", job.fuel_type);

    snprintf(buf, sizeof(buf), "%0.3f", job.volume_l);
    _right("Volume (l) : ", buf);
    snprintf(buf, sizeof(buf), "%0.2f", job.unit_price);
    _right("Unit Price (Rs) : ", buf);

    _rule(buf, sizeof(buf), job.total_price, '-');
    _right("  ", buf);
    _raw(k_font_a_dh, sizeof(k_font_a_dh));
    snprintf(buf, sizeof(buf), "Rs %0.2f", job.total_price);
    _right("Total :", buf);
    _raw(k_font_a, sizeof(k_font_a));
    _rule(buf, sizeof(buf), job.total_price, '=');
    _right("  ", buf);

    _fill("", '.');
    _center("Signature");
    _thank_you(cfg, 2);
    _tail(cfg);
}

void ReceiptRenderer::_theme2(const receipt_cfg_t &cfg, const receipt_job_t &job, const char *bill)
{
    char buf[64], left[40], right[40];
    _header_theme2(cfg);
    _cmd_body_reset();

    if (cfg.en_print_time) _right("Bill Issued : ", job.print_time);

    dt_t dt;
    _parse_dt(job.time_stamp, &dt);
    snprintf(left,  sizeof(left),  "Date : %04d-%02d-%02d", dt.year, dt.month, dt.day);
    snprintf(right, sizeof(right), "Pump ID : %s", _s(job.nozzle_id));
    _right(left, right);
    snprintf(left, sizeof(left), "Time : %02d:%02d:%02d", dt.hour, dt.minute, dt.second);
    _right(left, "");
    _right("Bill : ", bill);
    _center(job.print_note);
    _fill("", '-');

    _fill("", ' ');
    _fill("Vehicle No : ", '.');
    _fill("Order No : ", '.');
    _fill("", ' ');
    _right("Fuel Type : ", job.fuel_type);

    snprintf(buf, sizeof(buf), "%0.2f", job.unit_price);
    _right("Unit Price (Rs.): ", buf);
    snprintf(buf, sizeof(buf), "%0.3f", job.volume_l);
    _right("Issued Qty. (lit): ", buf);

    _rule(buf, sizeof(buf), job.total_price, '-');
    _right("  ", buf);
    snprintf(buf, sizeof(buf), "Rs %0.2f", job.total_price);
    _right("Total :", buf);
    _rule(buf, sizeof(buf), job.total_price, '=');
    _right("  ", buf);

    _fill("", '.');
    _center("Signature");
    _thank_you(cfg, 2);
    _tail(cfg);
}

void ReceiptRenderer::_theme3(const receipt_cfg_t &cfg, const receipt_job_t &job, const char *bill)
{
    char buf[64], label[64];
    const char *note = _s(job.print_note);
    _header_theme3(cfg);
    _cmd_body_reset();
    _fill("", '-');

    if (strstr(note, "Original") || (job.has_event_id && strstr(note, "manual")))
        snprintf(label, sizeof(label), "Bill Tr Number : ");
    else if (job.has_event_id && strstr(note, "Copy"))
        snprintf(label, sizeof(label), "Copy Bill Tr Number : ");
    else
        snprintf(label, sizeof(label), "%s Bill Tr Number : ", note);
    _right(label, bill);

    if (cfg.en_print_time) _right("Bill Issued : ", job.print_time);
    _str("\n");

    _fill("Vehicle Number : ", '.');
    _printable_time(job.time_stamp, buf, sizeof(buf));
    _right("Fueled Time    : ", buf);
    snprintf(label, sizeof(label), "%s (%s)", _s(job.nozzle_id), _s(job.fuel_type));
    _right("Dispenser ID   : ", label);
    _str("\n");

    _raw(k_font_b_dh, sizeof(k_font_b_dh));
    snprintf(buf, sizeof(buf), "%0.3f    ", job.volume_l);
    _right("   Reading (Lt) : ", buf);
    snprintf(buf, sizeof(buf), "%0.2f    ", job.unit_price);
    _right("Unit Price (Rs) : ", buf);
    snprintf(buf, sizeof(buf), "%0.2f    ", job.total_price);
    _right("     Total (Rs) : ", buf);
    _raw(k_font_b, sizeof(k_font_b));
    _str("\n");

    if (cfg.en_signature) {
        _str("\n");
        _fill("", '.');
        _center("Signature");
    }
    _thank_you(cfg, 6);
    _tail(cfg);
}

// ── Public API ──────────────────────────────────────────────────────────────

size_t ReceiptRenderer::render_receipt(const receipt_cfg_t &cfg, const receipt_job_t &job, uint32_t bill_no)
{
    _reset();
    char bill[48];
    if (job.has_event_id && _s(job.event_id)[0])
        snprintf(bill, sizeof(bill), "%s", job.event_id);
    else
        snprintf(bill, sizeof(bill), "%lu", (unsigned long)bill_no);

    _width = cfg.printer_dots ? cfg.printer_dots : 35;
    if (_width > LINE_MAX - 2) _width = LINE_MAX - 2;

    switch (cfg.theme) {
        case 2:  _theme2(cfg, job, bill); break;
        case 3:  _theme3(cfg, job, bill); break;
        default: _theme1(cfg, job, bill); break;
    }
    return _overflow ? 0 : _len;
}

size_t ReceiptRenderer::render_totalizer(const receipt_cfg_t &cfg, const receipt_job_t &job)
{
    _reset();
    char buf[64];
    _width = 40;                       // legacy: PRINTER_WIDTH = 40 for totalizer slips
    _header_theme3(cfg);
    _cmd_body_reset();
    _fill("", '-');
    _str("\n");
    _printable_time(job.time_stamp, buf, sizeof(buf));
    _right("Totalized Time    : ", buf);
    _right("Dispenser ID      : ", job.nozzle_id);
    _str("\n");
    _raw(k_font_b_dh, sizeof(k_font_b_dh));
    _right("Reading (Lt) : ", job.totalizer);
    _raw(k_font_b, sizeof(k_font_b));
    _str("\n");
    _center(_s(cfg.footer)[0] ? cfg.footer : k_default_footer);
    _blank_lines(cfg.end_lines, 15);
    if (cfg.en_cut) {
        _raw(k_cut, sizeof(k_cut));
        _blank_lines(cfg.lines_after_cut, 15);
    }
    return _overflow ? 0 : _len;
}

size_t ReceiptRenderer::render_info(const receipt_cfg_t &cfg, const char *mac, const char *ip,
                                    const char *version, uint32_t bill_no)
{
    _reset();
    char buf[48];
    _width = 35;                       // legacy boot slip uses the initial width
    _raw(k_init, sizeof(k_init));
    _raw(k_codepage0, sizeof(k_codepage0));
    _raw(k_unknown2, sizeof(k_unknown2));
    _raw(k_roll_end, sizeof(k_roll_end));
    _raw(k_size_normal, sizeof(k_size_normal));
    _raw(k_font_b, sizeof(k_font_b));
    _center(mac);
    _center(ip);
    snprintf(buf, sizeof(buf), "v%s", _s(version));
    _center(buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)bill_no);
    _center(buf);
    _blank_lines(cfg.end_lines, 15);
    return _overflow ? 0 : _len;
}

receipt_job_t ReceiptRenderer::sample_job()
{
    receipt_job_t j{};
    j.time_stamp   = "2025-01-01T01:23:45";
    j.print_time   = "2025-01-01T01:23:45";
    j.print_note   = "TEST";
    j.nozzle_id    = "ABC 01";
    j.fuel_type    = "Sample Type";
    j.event_id     = "";
    j.totalizer    = "";
    j.has_event_id = false;
    j.volume_l     = 5.678;
    j.unit_price   = 123.45;
    j.total_price  = 700.95;
    return j;
}
