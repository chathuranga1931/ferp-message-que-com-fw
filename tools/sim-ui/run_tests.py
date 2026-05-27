#!/usr/bin/env python3
"""
run_tests.py — ferp-com automated test runner.

Usage
-----
    # With simulator already running:
    python3 run_tests.py --suite pumping_sanki
    python3 run_tests.py --suite pumping_censtar6

    # Run all suites in sequence (Sanki first, then Censtar 6):
    python3 run_tests.py --suite all

    # Let the runner launch the simulator automatically:
    python3 run_tests.py --suite all --launch-sim

    # All options:
    python3 run_tests.py \\
        --host 127.0.0.1 \\
        --port 9000 \\
        --suite all \\
        [--launch-sim] \\
        [--sim-binary /path/to/ferp-com-simulator] \\
        [--sim-log]

Available suites
----------------
    pumping_sanki    Sanki 6-digit pump transaction tests (below 1K/10K/100K/1M total)
    pumping_censtar6 Censtar 6-digit pump transaction tests (below 1K/10K/100K/200K total)
    all              Run pumping_sanki then pumping_censtar6 in sequence

Note: when using --launch-sim with 'all', the setup for pumping_censtar6
will automatically change display_type to 1 (Censtar) and reboot the
simulator, so no manual reconfiguration is needed.

Exit code
---------
    0   all tests passed
    1   one or more tests failed or connection error
"""

import argparse
import sys
import os

# Ensure tools/sim-ui/ is on the path when invoked from any directory
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from atu import AutomatedTestUnit, SimLauncher
from tests.test_pumping_sanki   import SankiPumpingTests
from tests.test_pumping_censtar6 import Censtar6PumpingTests

# ── Suite registry ────────────────────────────────────────────────────────────
# Add new suites here.  Each value is a class with run_all(self) -> list[dict].
SUITES = {
    "pumping_sanki":    SankiPumpingTests,
    "pumping_censtar6": Censtar6PumpingTests,
}

# Ordered list used by the 'all' meta-suite (runs suites in this sequence).
SUITE_ORDER = ["pumping_sanki", "pumping_censtar6"]


# ── Reporting ─────────────────────────────────────────────────────────────────

def _print_summary(results: list[dict]) -> int:
    """Print pass/fail summary.  Returns number of failed tests."""
    passed  = [r for r in results if r.get("passed")]
    failed  = [r for r in results if not r.get("passed")]

    print("\n" + "─" * 56)
    print("SUMMARY")
    print("─" * 56)
    for r in results:
        icon = "✓" if r.get("passed") else "✗"
        name = r.get("name", "?")
        note = ""
        if not r.get("passed"):
            note = f"  ({r.get('reason', 'assertion failed')})"
        print(f"  {icon}  {name}{note}")

    print("─" * 56)
    print(f"  {len(passed)} / {len(results)} passed")

    return len(failed)


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(
        description="ferp-com automated test runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--host",       default="127.0.0.1",
                    help="Simulator backdoor host (default: 127.0.0.1)")
    ap.add_argument("--port",       type=int, default=9000,
                    help="Simulator backdoor port (default: 9000)")
    ap.add_argument("--suite",      default="pumping_sanki",
                    choices=list(SUITES) + ["all"],
                    help="Test suite to run; 'all' runs pumping_sanki then "
                         "pumping_censtar6 in sequence (default: pumping_sanki)")
    ap.add_argument("--launch-sim", action="store_true",
                    help="Start the simulator binary before running tests")
    ap.add_argument("--sim-binary", default=None,
                    help="Path to ferp-com-simulator binary (auto-detected if omitted)")
    ap.add_argument("--sim-log",    action="store_true",
                    help="Print simulator stdout/stderr to console")
    args = ap.parse_args()

    # ── Optionally launch the simulator ──────────────────────────────────────
    launcher = None
    if args.launch_sim:
        print(f"Starting simulator on port {args.port}…")
        try:
            launcher = SimLauncher(
                binary_path=args.sim_binary,
                port=args.port,
                log_output=args.sim_log,
            )
            launcher.start()
            print("Simulator started.\n")
        except RuntimeError as e:
            print(f"ERROR: {e}")
            return 1

    # ── Connect AutomatedTestUnit ─────────────────────────────────────────────
    print(f"Connecting to simulator at {args.host}:{args.port}…")
    atu = AutomatedTestUnit(host=args.host, port=args.port)
    if not atu.connect():
        print(f"ERROR: Cannot connect to {args.host}:{args.port}")
        if launcher:
            launcher.stop()
        return 1
    print("Connected.\n")

    # ── Run suite(s) ──────────────────────────────────────────────────────────
    suite_names = SUITE_ORDER if args.suite == "all" else [args.suite]

    all_results: list[dict] = []
    try:
        for suite_name in suite_names:
            print(f"\n{'═' * 56}")
            print(f"Running suite: {suite_name}")
            print(f"{'═' * 56}")
            suite   = SUITES[suite_name](atu, launcher=launcher)
            results = suite.run_all()
            all_results.extend(results)
    except Exception as e:
        print(f"\nERROR during test run: {e}")
        atu.close()
        if launcher:
            launcher.stop()
        return 1

    # ── Report ────────────────────────────────────────────────────────────────
    failures = _print_summary(all_results)

    atu.close()
    if launcher:
        launcher.stop()

    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
