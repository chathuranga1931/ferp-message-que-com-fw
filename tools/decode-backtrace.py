#!/usr/bin/env python3
"""
decode-backtrace.py — Decode an ESP32 backtrace string into file/line/function.

Usage (crash.json — elf auto-located from fw_version):
    python3 decode-backtrace.py --crash-json crash.json
    python3 decode-backtrace.py --crash-json crash.json --elf path/to/fw.elf

Usage (plain backtrace text file):
    python3 decode-backtrace.py --backtrace-file bt.txt --elf path/to/fw.elf

Crash JSON format (produced by this firmware):
    {"events":[{"body":{"fw_version":"1.0.0.131",
                        "reset_reason":"task_wdt",
                        "rtc_valid":true,
                        "uptime_ms":33172,
                        "epoch_sec":1782327267,
                        "heap_free":56408,
                        "backtrace":"0x400e402e:0x3ffd3240 ..."}}]}

When a .json is given without an explicit elf_file the script searches:
    releases/<fw_version>/ferp-com-v<fw_version>.elf
relative to the workspace root (the directory that contains this script's
parent folder, i.e. <repo_root>/tools/../).

Plain backtrace file:
    One line of space-separated PC:SP pairs per backtrace block.
    Lines starting with '#' are treated as comments and skipped.

The script auto-detects xtensa-esp32-elf-addr2line from ~/.espressif.
Override with --addr2line <path>.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone

# ── Workspace root (parent of tools/) ────────────────────────────────────────

SCRIPT_DIR    = os.path.dirname(os.path.realpath(__file__))
WORKSPACE_ROOT = os.path.dirname(SCRIPT_DIR)   # tools/../ == repo root

# ── Toolchain auto-detection ─────────────────────────────────────────────────

def _find_addr2line() -> str:
    """Search ~/.espressif for xtensa-esp32-elf-addr2line, newest version first."""
    pattern = os.path.expanduser(
        "~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32-elf-addr2line"
    )
    candidates = sorted(glob.glob(pattern), reverse=True)
    if candidates:
        return candidates[0]
    for name in ("xtensa-esp32-elf-addr2line", "xtensa-esp-elf-addr2line"):
        result = subprocess.run(["which", name], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()
    return ""


# ── ELF auto-location ─────────────────────────────────────────────────────────

def _find_elf(version: str) -> str:
    """
    Search for the ELF matching fw_version under releases/.
    Pattern: releases/<version>/ferp-com-v<version>.elf
    """
    candidate = os.path.join(
        WORKSPACE_ROOT, "releases", version, f"ferp-com-v{version}.elf"
    )
    if os.path.isfile(candidate):
        return candidate

    # Broader glob fallback — any .elf inside releases/<version>/
    pattern = os.path.join(WORKSPACE_ROOT, "releases", version, "*.elf")
    hits = sorted(glob.glob(pattern))
    # Prefer the one whose name contains the exact version
    for h in hits:
        if version in os.path.basename(h):
            return h
    if hits:
        return hits[0]
    return ""


# ── JSON input handling ───────────────────────────────────────────────────────

def _fmt_epoch(epoch_sec: int) -> str:
    try:
        dt = datetime.fromtimestamp(epoch_sec, tz=timezone.utc)
        return dt.strftime("%Y-%m-%d %H:%M:%S UTC")
    except Exception:
        return str(epoch_sec)


def _fmt_uptime(uptime_ms: int) -> str:
    s  = uptime_ms // 1000
    ms = uptime_ms % 1000
    h, rem = divmod(s, 3600)
    m, s   = divmod(rem, 60)
    return f"{h}h {m:02d}m {s:02d}.{ms:03d}s  ({uptime_ms} ms)"


def load_json(path: str) -> tuple[list[str], str, dict]:
    """
    Parse a crash JSON file.
    Returns (backtrace_lines, fw_version, meta_dict).
    meta_dict holds display fields (reset_reason, uptime_ms, etc.).
    """
    with open(path) as fh:
        data = json.load(fh)

    events = data.get("events", [])
    if not events:
        sys.exit("[ERROR] JSON has no 'events' array.")

    backtrace_lines = []
    fw_version      = ""
    meta            = {}

    for evt in events:
        body = evt.get("body", {})
        bt   = body.get("backtrace", "").strip()
        if bt:
            backtrace_lines.append(bt)
        if not fw_version:
            fw_version = body.get("fw_version", "")
        if not meta and bt:
            meta = {
                "fw_version":   body.get("fw_version",   ""),
                "reset_reason": body.get("reset_reason", ""),
                "rtc_valid":    body.get("rtc_valid",    None),
                "uptime_ms":    body.get("uptime_ms",    0),
                "epoch_sec":    body.get("epoch_sec",    0),
                "heap_free":    body.get("heap_free",    0),
                "time":         evt.get("time",          ""),
                "device":       evt.get("device",        ""),
            }

    if not backtrace_lines:
        sys.exit("[ERROR] No 'backtrace' field found in JSON events.")

    return backtrace_lines, fw_version, meta


def print_crash_header(meta: dict, elf: str) -> None:
    print("=" * 72)
    print("  CRASH REPORT")
    print("=" * 72)
    if meta.get("device"):
        print(f"  Device       : {meta['device']}")
    if meta.get("time"):
        print(f"  Time         : {meta['time']}")
    if meta.get("fw_version"):
        print(f"  FW version   : {meta['fw_version']}")
    if meta.get("reset_reason"):
        print(f"  Reset reason : {meta['reset_reason']}")
    rtc = meta.get("rtc_valid")
    if rtc is not None:
        print(f"  RTC valid    : {rtc}")
    if meta.get("uptime_ms"):
        print(f"  Uptime       : {_fmt_uptime(meta['uptime_ms'])}")
    if meta.get("epoch_sec"):
        print(f"  Epoch        : {_fmt_epoch(meta['epoch_sec'])}")
    if meta.get("heap_free"):
        print(f"  Heap free    : {meta['heap_free']:,} bytes")
    print(f"  ELF          : {elf}")
    print("=" * 72)
    print()


# ── Backtrace parsing ─────────────────────────────────────────────────────────

_ADDR_RE = re.compile(r"0x[0-9a-fA-F]+:0x[0-9a-fA-F]+")

def parse_backtrace_line(line: str) -> list[str]:
    pairs = _ADDR_RE.findall(line)
    return [pair.split(":")[0] for pair in pairs]


# ── addr2line invocation ──────────────────────────────────────────────────────

def decode_addresses(addr2line: str, elf: str, addresses: list[str]) -> list[dict]:
    if not addresses:
        return []

    cmd    = [addr2line, "-f", "-C", "-e", elf, "-p", "-i"] + addresses
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"[ERROR] addr2line failed: {result.stderr.strip()}", file=sys.stderr)
        sys.exit(1)

    frames    = []
    addr_iter = iter(addresses)
    pc        = next(addr_iter, None)

    for raw in result.stdout.splitlines():
        raw = raw.strip()
        if not raw:
            continue
        inline = raw.startswith("(inlined by)")
        if inline:
            raw = raw[len("(inlined by)"):].strip()
        if " at " in raw:
            func_part, loc_part = raw.split(" at ", 1)
        else:
            func_part = raw
            loc_part  = "??:0"

        frames.append({
            "pc":       pc if not inline else "(inlined)",
            "inline":   inline,
            "function": func_part.strip(),
            "location": loc_part.strip(),
        })
        if not inline:
            pc = next(addr_iter, None)

    return frames


# ── Formatting ────────────────────────────────────────────────────────────────

def print_frames(frames: list[dict]) -> None:
    frame_idx = 0
    for f in frames:
        loc = f["location"]
        loc = re.sub(r".*/esp-idf/",             "esp-idf/",  loc)
        loc = re.sub(r".*/ferp-message-library/", "",         loc)

        if f["inline"]:
            print(f"             (inlined) | {f['function']:<50} @ {loc}")
        else:
            print(f"  Frame {frame_idx:>3} | PC {f['pc']:<14} | {f['function']:<50} @ {loc}")
            frame_idx += 1


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Decode an ESP32 backtrace (crash.json or plain text file).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python3 decode-backtrace.py --crash-json crash.json\n"
            "  python3 decode-backtrace.py --crash-json crash.json --elf path/to/fw.elf\n"
            "  python3 decode-backtrace.py --backtrace-file bt.txt --elf path/to/fw.elf\n"
        ),
    )
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--crash-json",     metavar="PATH",
                     help="Crash report JSON file (fw_version used to auto-locate ELF)")
    src.add_argument("--backtrace-file", metavar="PATH",
                     help="Plain text file with PC:SP backtrace lines")

    parser.add_argument("--elf",       default="", metavar="PATH",
                        help="Path to .elf — required for --backtrace-file; "
                             "optional for --crash-json (auto-located from fw_version)")
    parser.add_argument("--addr2line", default="", metavar="PATH",
                        help="Override path to xtensa-esp32-elf-addr2line")
    args = parser.parse_args()

    addr2line = args.addr2line or _find_addr2line()
    if not addr2line:
        sys.exit(
            "[ERROR] xtensa-esp32-elf-addr2line not found.\n"
            "        Install ESP-IDF tools or pass --addr2line <path>."
        )

    # ── JSON input ───────────────────────────────────────────────────────────
    if args.crash_json:
        if not os.path.isfile(args.crash_json):
            sys.exit(f"[ERROR] crash-json not found: {args.crash_json}")

        backtrace_lines, fw_version, meta = load_json(args.crash_json)

        elf = args.elf or _find_elf(fw_version)
        if not elf:
            sys.exit(
                f"[ERROR] ELF not found for fw_version '{fw_version}'.\n"
                f"        Expected: releases/{fw_version}/ferp-com-v{fw_version}.elf\n"
                f"        Pass it explicitly with --elf <path>"
            )
        if not os.path.isfile(elf):
            sys.exit(f"[ERROR] ELF file not found: {elf}")

        print_crash_header(meta, elf)

    # ── Plain text input ─────────────────────────────────────────────────────
    else:
        if not os.path.isfile(args.backtrace_file):
            sys.exit(f"[ERROR] backtrace-file not found: {args.backtrace_file}")
        if not args.elf:
            sys.exit("[ERROR] --elf is required when using --backtrace-file")
        elf = args.elf
        if not os.path.isfile(elf):
            sys.exit(f"[ERROR] ELF file not found: {elf}")

        print(f"addr2line : {addr2line}")
        print(f"elf       : {elf}")
        print()

        with open(args.backtrace_file) as fh:
            backtrace_lines = [
                l.strip() for l in fh
                if l.strip() and not l.strip().startswith("#")
            ]

    # ── Decode ───────────────────────────────────────────────────────────────
    for idx, bt_line in enumerate(backtrace_lines, 1):
        pc_list = parse_backtrace_line(bt_line)
        if not pc_list:
            continue

        if len(backtrace_lines) > 1:
            print(f"=== Backtrace {idx} ({len(pc_list)} frames) " + "=" * 40)
        else:
            print(f"Backtrace ({len(pc_list)} frames)")
        print(f"  Raw: {bt_line}")
        print()

        frames = decode_addresses(addr2line, elf, pc_list)
        if frames:
            print_frames(frames)
        else:
            print("  (no frames decoded)")
        print()


if __name__ == "__main__":
    main()
