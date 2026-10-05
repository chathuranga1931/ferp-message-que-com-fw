/**
 * Receipt preview — TypeScript port of the printer firmware's ReceiptRenderer
 * (src/app-modules/module_printer/receipt_renderer.cpp). Same line rules, same
 * texts; ESC/POS font changes become a `big` flag (double height) per line.
 * Keep the two in step when a layout changes.
 */

export interface ReceiptCfg {
  name_l1: string; name_l2: string; add_l1: string; add_l2: string; tele: string;
  thank: string[];            // 6 lines
  footer: string;
  theme: number;              // 1 | 2 | 3
  printer_dots: number;       // characters per line
  end_lines: number;
  lines_after_cut: number;
  en_print_time: boolean;
  en_signature: boolean;
  en_cut: boolean;
}

export interface ReceiptJob {
  time_stamp: string; print_time: string; print_note: string; nozzle_id: string;
  fuel_type: string; event_id: string; totalizer: string; has_event_id: boolean;
  volume_l: number; unit_price: number; total_price: number;
}

export interface ReceiptLine { text: string; big?: boolean; cut?: boolean }

const DEFAULT_FOOTER = "www.myfuelstation.net (0712209310)";

export const SAMPLE_JOB: ReceiptJob = {
  time_stamp: "2025-01-01T01:23:45", print_time: "2025-01-01T01:23:45", print_note: "TEST",
  nozzle_id: "ABC 01", fuel_type: "Sample Type", event_id: "", totalizer: "", has_event_id: false,
  volume_l: 5.678, unit_price: 123.45, total_price: 700.95,
};

function parseDt(s: string) {
  const m = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})/.exec(s ?? "");
  if (!m) return null;
  return { y: m[1], mo: m[2], d: m[3], h: m[4], mi: m[5], s: m[6] };
}

class Builder {
  lines: ReceiptLine[] = [];
  big = false;
  constructor(public width: number) {}
  push(text: string) { this.lines.push({ text, big: this.big }); }
  right(left: string, right: string) {
    const spaces = this.width - left.length - right.length;
    if (spaces < 0) this.push(left.slice(0, Math.max(0, this.width - right.length)) + right);
    else this.push(left + " ".repeat(spaces) + right);
  }
  center(text: string) {
    const spaces = this.width - text.length;
    if (spaces <= 0) { this.push(text.slice(0, this.width)); return; }
    const pad = Math.floor((spaces + 1) / 2);
    this.push(" ".repeat(pad) + text.slice(0, this.width - pad));
  }
  fill(prefix: string, c: string) { this.push(prefix + c.repeat(Math.max(0, this.width - prefix.length))); }
  blank(n: number) { for (let i = 0; i < Math.min(n, 15); i++) this.push(""); }
}

const f2 = (n: number) => n.toFixed(2);
const f3 = (n: number) => n.toFixed(3);
const rule = (total: number, c: string) => c.repeat(`Rs ${f2(total)}`.length);

function header(b: Builder, cfg: ReceiptCfg, theme: number) {
  if (theme === 3) {
    b.big = true;
    if (cfg.name_l1) b.right(cfg.name_l1, "");
    if (cfg.name_l2) b.right(cfg.name_l2, "");
    b.big = false;
  } else {
    b.big = true;
    if (cfg.name_l1) b.center(cfg.name_l1);
    b.big = false;
    if (cfg.name_l2) b.center(cfg.name_l2);
  }
  if (cfg.add_l1) b.center(cfg.add_l1);
  if (cfg.add_l2) b.center(cfg.add_l2);
  if (cfg.tele) b.center(cfg.tele);
}

function tail(b: Builder, cfg: ReceiptCfg) {
  b.center(cfg.footer || DEFAULT_FOOTER);
  b.blank(cfg.end_lines);
  if (cfg.en_cut) {
    b.lines.push({ text: "", cut: true });
    b.blank(cfg.lines_after_cut);
  }
}

export function renderReceipt(cfg: ReceiptCfg, job: ReceiptJob, billNo: number): ReceiptLine[] {
  const theme = [1, 2, 3].includes(cfg.theme) ? cfg.theme : 1;
  const b = new Builder(Math.min(cfg.printer_dots || 35, 254));
  const bill = job.has_event_id && job.event_id ? job.event_id : String(billNo);
  header(b, cfg, theme);

  if (theme === 1) {
    if (cfg.en_print_time) b.right("Bill Issued : ", job.print_time);
    b.right("Bill Number : ", bill);
    b.center(job.print_note);
    b.fill("", "-");
    b.fill("Vehicle Nu : ", ".");
    b.right("Fueled Time : ", job.time_stamp);
    b.right("Pump ID : ", job.nozzle_id);
    b.right("Fuel Catogory : ", job.fuel_type);
    b.right("Volume (l) : ", f3(job.volume_l));
    b.right("Unit Price (Rs) : ", f2(job.unit_price));
    b.right("  ", rule(job.total_price, "-"));
    b.big = true; b.right("Total :", `Rs ${f2(job.total_price)}`); b.big = false;
    b.right("  ", rule(job.total_price, "="));
    b.fill("", "."); b.center("Signature");
    cfg.thank.slice(0, 2).forEach((t) => t && b.center(t));
  } else if (theme === 2) {
    if (cfg.en_print_time) b.right("Bill Issued : ", job.print_time);
    const dt = parseDt(job.time_stamp) ?? { y: "0000", mo: "00", d: "00", h: "00", mi: "00", s: "00" };
    b.right(`Date : ${dt.y}-${dt.mo}-${dt.d}`, `Pump ID : ${job.nozzle_id}`);
    b.right(`Time : ${dt.h}:${dt.mi}:${dt.s}`, "");
    b.right("Bill : ", bill);
    b.center(job.print_note);
    b.fill("", "-"); b.fill("", " ");
    b.fill("Vehicle No : ", "."); b.fill("Order No : ", "."); b.fill("", " ");
    b.right("Fuel Type : ", job.fuel_type);
    b.right("Unit Price (Rs.): ", f2(job.unit_price));
    b.right("Issued Qty. (lit): ", f3(job.volume_l));
    b.right("  ", rule(job.total_price, "-"));
    b.right("Total :", `Rs ${f2(job.total_price)}`);
    b.right("  ", rule(job.total_price, "="));
    b.fill("", "."); b.center("Signature");
    cfg.thank.slice(0, 2).forEach((t) => t && b.center(t));
  } else {
    b.fill("", "-");
    const note = job.print_note ?? "";
    let label: string;
    if (note.includes("Original") || (job.has_event_id && note.includes("manual"))) label = "Bill Tr Number : ";
    else if (job.has_event_id && note.includes("Copy")) label = "Copy Bill Tr Number : ";
    else label = `${note} Bill Tr Number : `;
    b.right(label, bill);
    if (cfg.en_print_time) b.right("Bill Issued : ", job.print_time);
    b.push("");
    b.fill("Vehicle Number : ", ".");
    const dt = parseDt(job.time_stamp);
    b.right("Fueled Time    : ", dt ? `${dt.d}/${dt.mo}/${dt.y}  ${dt.h}:${dt.mi}:${dt.s}` : "Invalid Time");
    b.right("Dispenser ID   : ", `${job.nozzle_id} (${job.fuel_type})`);
    b.push("");
    b.big = true;
    b.right("   Reading (Lt) : ", `${f3(job.volume_l)}    `);
    b.right("Unit Price (Rs) : ", `${f2(job.unit_price)}    `);
    b.right("     Total (Rs) : ", `${f2(job.total_price)}    `);
    b.big = false;
    b.push("");
    if (cfg.en_signature) { b.push(""); b.fill("", "."); b.center("Signature"); }
    cfg.thank.forEach((t) => t && b.center(t));
  }
  tail(b, cfg);
  return b.lines;
}

export function renderTotalizer(cfg: ReceiptCfg, job: ReceiptJob): ReceiptLine[] {
  const b = new Builder(40);
  header(b, cfg, 3);
  b.fill("", "-"); b.push("");
  const dt = parseDt(job.time_stamp);
  b.right("Totalized Time    : ", dt ? `${dt.d}/${dt.mo}/${dt.y}  ${dt.h}:${dt.mi}:${dt.s}` : "Invalid Time");
  b.right("Dispenser ID      : ", job.nozzle_id);
  b.push("");
  b.big = true; b.right("Reading (Lt) : ", job.totalizer); b.big = false;
  b.push("");
  tail(b, cfg);
  return b.lines;
}

/** Build the preview config from config-key values (as read from the device). */
export function cfgFromValues(get: (name: string) => string | undefined): ReceiptCfg {
  const s = (n: string) => get(n) ?? "";
  const n = (k: string, d: number) => { const v = Number(get(k)); return Number.isFinite(v) && get(k) !== undefined ? v : d; };
  const b = (k: string, d: boolean) => { const v = get(k); return v === undefined ? d : v === "True" || v === "true" || v === "1"; };
  return {
    name_l1: s("PRN_NAME_L1"), name_l2: s("PRN_NAME_L2"), add_l1: s("PRN_ADD_L1"), add_l2: s("PRN_ADD_L2"),
    tele: s("PRN_TELE"),
    thank: [1, 2, 3, 4, 5, 6].map((i) => s(`PRN_THANK_L${i}`)),
    footer: s("PRN_FOOTER"),
    theme: n("PRN_THEME", 1), printer_dots: n("PRN_PRINTER_DOTS", 35),
    end_lines: n("PRN_END_LINES", 4), lines_after_cut: n("PRN_LINES_AFTER_CUT", 0),
    en_print_time: b("PRN_EN_PRINT_TIME", false), en_signature: b("PRN_EN_SIGNATURE", true),
    en_cut: b("PRN_EN_CUT", false),
  };
}
