#!/usr/bin/env python3
"""
decode-backtrace.py — Decode an ESP32 backtrace string into file/line/function.

Usage:
    python3 decode-backtrace.py <backtrace_file> <elf_file>

    <backtrace_file>  Text file containing one line of space-separated PC:SP pairs:
                        0x400e402e:0x3ffd3240 0x400fe4c9:0x3ffd3270 ...
                      Multiple lines are accepted; each line is decoded separately.
                      Lines starting with '#' are treated as comments and skipped.

    <elf_file>        Path to the .elf file produced by the build.

Example:
    python3 decode-backtrace.py crash.txt build/ferp-com.elf

Output example:
    Frame  0 | PC 0x400e402e | some_function       at src/foo.cpp:123
    Frame  1 | PC 0x400fe4c9 | another_function    at src/bar.cpp:456
    ...

The script auto-detects the xtensa-esp32-elf-addr2line binary from the
~/.espressif toolchain directory. You can override it with --addr2line <path>.
"""

import argparse
import glob
import os
import re
import subprocess
import sys

# ── Toolchain auto-detection ─────────────────────────────────────────────────

def _find_addr2line() -> str:
    """Search ~/.espressif for xtensa-esp32-elf-addr2line, newest version first."""
    pattern = os.path.expanduser(
        "~/.espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32-elf-addr2line"
    )
    candidates = sorted(glob.glob(pattern), reverse=True)
    if candidates:
        return candidates[0]

    # Fallback: look in PATH
    for name in ("xtensa-esp32-elf-addr2line", "xtensa-esp-elf-addr2line"):
        result = subprocess.run(["which", name], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()

    return ""


# ── Backtrace parsing ─────────────────────────────────────────────────────────

_ADDR_RE = re.compile(r"0x[0-9a-fA-F]+:0x[0-9a-fA-F]+")

def parse_backtrace_line(line: str) -> list[str]:
    """Return list of PC address strings from one backtrace line."""
    pairs = _ADDR_RE.findall(line)
    return [pair.split(":")[0] for pair in pairs]  # only PC part


# ── addr2line invocation ──────────────────────────────────────────────────────

def decode_addresses(addr2line: str, elf: str, addresses: list[str]) -> list[dict]:
    """
    Call addr2line once for all addresses and return a list of dicts:
        { pc, function, file, line }
    addr2line flags:
        -f  print function name
        -C  demangle C++ names
        -e  elf file
        -p  pretty-print (one result per line, easier to parse)
        -i  inline frames
    """
    if not addresses:
        return []

    cmd = [addr2line, "-f", "-C", "-e", elf, "-p", "-i"] + addresses
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"[ERROR] addr2line failed: {result.stderr.strip()}", file=sys.stderr)
        sys.exit(1)

    # -p output: "function at file:line" or "?? at ??:0"
    # With -i each inlined frame appears on its own line.
    # We zip back with the original addresses: each non-inlined entry starts
    # a new frame.  We do simple line-per-address mapping here.
    lines = result.stdout.splitlines()
    frames = []
    addr_iter = iter(addresses)
    pc = next(addr_iter, None)

    for raw in lines:
        raw = raw.strip()
        if not raw:
            continue
        # Pattern: "function at file:lineno"  or  "(inlined by) function at ..."
        inline = raw.startswith("(inlined by)")
        if inline:
            raw = raw[len("(inlined by)"):].strip()
        if " at " in raw:
            func_part, loc_part = raw.split(" at ", 1)
        else:
            func_part = raw
            loc_part = "??:0"

        frames.append({
            "pc":      pc if not inline else "(inlined)",
            "inline":  inline,
            "function": func_part.strip(),
            "location": loc_part.strip(),
        })

        if not inline:
            pc = next(addr_iter, None)

    return frames


# ── Formatting ────────────────────────────────────────────────────────────────

def print_frames(frames: list[dict], pc_list: list[str]) -> None:
    frame_idx = 0
    for f in frames:
        prefix = f"  (inlined)" if f["inline"] else f"Frame {frame_idx:>3}"
        pc_str = f["pc"] if f["inline"] else f"{f['pc']}"
        func   = f["function"]
        loc    = f["location"]

        # Trim absolute paths to something readable
        loc = re.sub(r".*/esp-idf/", "esp-idf/", loc)
        loc = re.sub(r".*/ferp-message-library/", "", loc)

        print(f"  {prefix} | PC {pc_str:<12} | {func:<40} @ {loc}")

        if not f["inline"]:
            frame_idx += 1


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Decode an ESP32 backtrace string using addr2line."
    )
    parser.add_argument("backtrace_file", help="File containing backtrace PC:SP pairs")
    parser.add_argument("elf_file",       help="Path to the .elf build artifact")
    parser.add_argument("--addr2line",    default="", metavar="PATH",
                        help="Override path to xtensa-esp32-elf-addr2line")
    args = parser.parse_args()

    # Validate inputs
    if not os.path.isfile(args.backtrace_file):
        sys.exit(f"[ERROR] backtrace file not found: {args.backtrace_file}")
    if not os.path.isfile(args.elf_file):
        sys.exit(f"[ERROR] elf file not found: {args.elf_file}")

    addr2line = args.addr2line or _find_addr2line()
    if not addr2line:
        sys.exit(
            "[ERROR] xtensa-esp32-elf-addr2line not found.\n"
            "        Install ESP-IDF tools or pass --addr2line <path>."
        )
    if not os.path.isfile(addr2line):
        sys.exit(f"[ERROR] addr2line not found at: {addr2line}")

    print(f"addr2line : {addr2line}")
    print(f"elf       : {args.elf_file}")
    print()

    with open(args.backtrace_file) as fh:
        raw_lines = fh.readlines()

    block_num = 0
    for raw in raw_lines:
        raw = raw.strip()
        if not raw or raw.startswith("#"):
            continue

        pc_list = parse_backtrace_line(raw)
        if not pc_list:
            continue

        block_num += 1
        print(f"=== Backtrace {block_num} ({len(pc_list)} frames) " + "=" * 40)
        print(f"  Raw: {raw}")
        print()

        frames = decode_addresses(addr2line, args.elf_file, pc_list)
        if frames:
            print_frames(frames, pc_list)
        else:
            print("  (no frames decoded)")
        print()


if __name__ == "__main__":
    main()
