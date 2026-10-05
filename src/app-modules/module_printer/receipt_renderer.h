// receipt_renderer.h
//
// ReceiptRenderer — builds the complete ESC/POS byte stream for one receipt
// into a caller-supplied buffer.  Pure C++ (no PAL, no HSYS) so it can be
// unit-tested on a PC and reused by every printer transport (UART, USB).
//
// Layouts are a faithful port of the legacy Arduino printer firmware
// (old/ferp-printer/src/application/printer-device/printer.cpp):
//   theme 1 / 2 / 3 receipts, the totalizer slip and the boot/info slip.
// Fixes vs. legacy: bounded formatting (no 256-byte overruns), zeroed date
// fields when a timestamp cannot be parsed, theme 2 falls back to the bill
// counter when the request carries no bill number.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Receipt settings (pointers into the live app config; never null). */
typedef struct {
    const char *name_l1;
    const char *name_l2;
    const char *add_l1;
    const char *add_l2;
    const char *tele;
    const char *thank[6];
    const char *footer;          ///< bottom line, e.g. "www.myfuelstation.net (0712209310)"
    uint32_t    theme;           ///< 1, 2 or 3 (anything else → 1)
    uint32_t    printer_dots;    ///< characters per line for receipts
    uint32_t    end_lines;       ///< blank lines after the footer (max 15)
    uint32_t    lines_after_cut; ///< blank lines after the cut command (max 15)
    bool        en_print_time;   ///< print the "Bill Issued" line
    bool        en_signature;    ///< theme 3: print the signature block
    bool        en_cut;          ///< send the paper-cut command
} receipt_cfg_t;

/** One job (mirrors MsgPrintJob::Payload fields used for rendering). */
typedef struct {
    const char *time_stamp;      ///< fueled time, ISO "YYYY-MM-DDTHH:MM:SS..."
    const char *print_time;
    const char *print_note;
    const char *nozzle_id;
    const char *fuel_type;
    const char *event_id;
    const char *totalizer;
    bool        has_event_id;
    double      volume_l;
    double      unit_price;
    double      total_price;
} receipt_job_t;

class ReceiptRenderer
{
public:
    static constexpr size_t LINE_MAX = 256;

    ReceiptRenderer(uint8_t *buf, size_t cap) : _buf(buf), _cap(cap) {}

    /** Fuel receipt using cfg.theme. Returns bytes written (0 on overflow). */
    size_t render_receipt(const receipt_cfg_t &cfg, const receipt_job_t &job, uint32_t bill_no);

    /** Totalizer slip (theme 3 header, fixed 40 columns, as in legacy). */
    size_t render_totalizer(const receipt_cfg_t &cfg, const receipt_job_t &job);

    /** Boot / info slip: MAC, IP, "v<version>", bill counter (35 columns). */
    size_t render_info(const receipt_cfg_t &cfg, const char *mac, const char *ip,
                       const char *version, uint32_t bill_no);

    /** Fixed sample job used by /printSample and PRINTER_CMD_PRINT_SAMPLE. */
    static receipt_job_t sample_job();

    size_t length() const   { return _len; }
    bool   overflow() const { return _overflow; }

private:
    uint8_t *_buf;
    size_t   _cap;
    size_t   _len      = 0;
    bool     _overflow = false;
    uint32_t _width    = 35;
    char     _line[LINE_MAX];

    void _reset() { _len = 0; _overflow = false; }
    void _raw(const void *data, size_t n);
    void _str(const char *s);

    // Line builders (port of the legacy *_with_new_line_buff helpers)
    void _right(const char *left, const char *right);
    void _center(const char *text);
    void _fill(const char *prefix, char c);
    void _blank_lines(uint32_t n, uint32_t cap);

    void _cmd_body_reset();      // init + "unknown2/3" + normal size + font B
    void _header_theme1(const receipt_cfg_t &cfg);
    void _header_theme2(const receipt_cfg_t &cfg);
    void _header_theme3(const receipt_cfg_t &cfg);
    void _thank_you(const receipt_cfg_t &cfg, int lines);
    void _tail(const receipt_cfg_t &cfg);   // footer + end lines + optional cut

    void _theme1(const receipt_cfg_t &cfg, const receipt_job_t &job, const char *bill);
    void _theme2(const receipt_cfg_t &cfg, const receipt_job_t &job, const char *bill);
    void _theme3(const receipt_cfg_t &cfg, const receipt_job_t &job, const char *bill);
};
