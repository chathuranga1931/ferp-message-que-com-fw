"""
test_pumping_sanki.py — Sanki 6-digit pump transaction tests.

Setup flow (when launcher is provided)
---------------------------------------
  1. Set display type = DT_SANKI_6 (6) via MsgConfigSet
  2. Send MsgSystemReboot → simulator exits
  3. Launcher restarts the simulator
  4. ATU reconnects
  5. Verify display type reads back as 6

Test matrix
-----------
Each case targets a specific total-price range to exercise the Sanki 6-digit
display overflow correction in the firmware (sanki6_process_data):

  below_1k    total < $1,000    — no wrapping, simplest path
  below_10k   total < $10,000   — no wrapping, slightly higher vol
  below_100k  total < $100,000  — total_pricex100 wraps 1–9 times
  below_1000k total < $1,000,000 — total_pricex100 wraps many times

Sanki display constraints
-------------------------
  - vol_lx1000   wraps at 10,000,000  (6-digit volume: max 9,999.999 L)
  - total_x100   wraps at  1,000,000  (6-digit price:  max $9,999.99)
  - unit_price must be >= $2.00/L (unit_pricex100 >= 20,000)
    → use 42,300 ($4.23/L) to match hardware emulator

Tolerances
----------
  - vol_lx1000    ± 1 mL   (single pump tick at 100 ms intervals)
  - total_pricex100 ± 200  (±$2.00 — rounding across many overflow corrections)
"""

import sys
import os

# Allow running directly as a script: ensure tools/sim-ui/ is on sys.path
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

from atu import AutomatedTestUnit, DT_SANKI_6
from sim_core import encode_frame_sanki6

# ── Test parameters ───────────────────────────────────────────────────────────

UNIT_PRICE_X100 = 42_300      # $4.23 / L  (must be >= 20,000 for Sanki FW)
NOZZLE          = 0

# (test_name, target_total_pricex100, human_label)
CASES = [
    ("below_1k",     50_000,          "$500.00"),
    ("below_10k",    500_000,          "$5,000.00"),
    ("below_100k",   5_000_000,        "$50,000.00"),
    ("below_1000k",  50_000_000,       "$500,000.00"),
]

VOL_TOLERANCE   = 1      # mL
TOTAL_TOLERANCE = 200    # price × 100  (~$2.00 — correction rounding allowance)

# Configurable pump rate (mL/s) used for every case in this suite.
# Sized so the below_1000k full-rate section completes in ~200 frames at
# 50 ms intervals.  sim_core.py applies 25 % of this rate for the first
# and last 1 L automatically (_RAMP_FRACTION / _RAMP_RATE_CAP_ML_S).
PUMP_RATE_ML_S  = 118_000


# ── Helper ────────────────────────────────────────────────────────────────────

def _target_vol(target_total_x100: int) -> int:
    """Back-compute volume (mL) from target total price."""
    return (target_total_x100 * 1000) // UNIT_PRICE_X100


# ── Test suite ────────────────────────────────────────────────────────────────

class SankiPumpingTests:
    """
    Runs the Sanki 6-digit pumping test matrix through an AutomatedTestUnit.

    Usage from run_tests.py:
        suite  = SankiPumpingTests(atu, launcher=launcher)
        results = suite.run_all()
    """

    def __init__(self, atu: AutomatedTestUnit, launcher=None):
        self._atu      = atu
        self._launcher = launcher

    def run_all(self) -> list[dict]:
        # ── Setup: configure display type and reboot ──────────────────────────
        setup_ok = self._setup()
        if not setup_ok:
            return [{"name": "setup", "passed": False, "reason": "setup failed"}]

        results = []
        for name, target_total, label in CASES:
            results.append(self._run_case(name, target_total, label))
        return results

    # ── Setup ─────────────────────────────────────────────────────────────────

    def _setup(self) -> bool:
        """
        Set display type to Sanki 6-digit, reboot the device, and verify.

        If no launcher is provided (simulator managed externally), skips the
        reboot and only verifies the current display type.
        """
        if self._launcher is not None:
            print("[SETUP] Setting display type = Sanki 6-digit (6)…")
            self._atu.set_display_type(DT_SANKI_6)
            import time; time.sleep(0.5)   # let ModuleConfig persist to SPIFFS

            print("[SETUP] Rebooting device…")
            ok = self._atu.reboot_and_reconnect(
                launcher        = self._launcher,
                wait_for_exit_s = 5.0,
                restart_wait_s  = 8.0,
            )
            if not ok:
                print("[SETUP] ERROR: failed to reconnect after reboot")
                return False
            print("[SETUP] Reconnected after reboot.")
            import time; time.sleep(3.0)   # let module_fuel finish its init chain
        else:
            print("[SETUP] No launcher — skipping display type config + reboot.")

        # Verify display type
        print("[SETUP] Reading display type…")
        dt = self._atu.get_display_type(timeout=5.0)
        if dt is None:
            print("[SETUP] WARNING: display type read timed out (continuing)")
        elif dt != DT_SANKI_6:
            print(f"[SETUP] WARNING: display type = {dt}, expected {DT_SANKI_6}")
        else:
            print(f"[SETUP] Display type confirmed = {dt} (Sanki 6-digit)  OK")

        return True

    # ── Individual cases ──────────────────────────────────────────────────────

    def _run_case(self, name: str, target_total_x100: int, label: str) -> dict:
        target_vol = _target_vol(target_total_x100)

        print(f"\n[TEST] sanki6::{name}")
        print(f"       target total  = {label}  (x100 = {target_total_x100})")
        print(f"       target volume = {target_vol / 1000:.3f} L  (lx1000 = {target_vol})")

        self._atu.monitor.clear()

        # Scale rate to target ~10 s per pump so all cases finish quickly.
        # Minimum 100 mL/s ensures at least 10 increments even for tiny pumps.
        rate_ml_s = PUMP_RATE_ML_S

        pumped = self._atu.pump(NOZZLE).run_transaction(
            display_type       = DT_SANKI_6,
            unit_price_x100    = UNIT_PRICE_X100,
            target_vol_lx1000  = target_vol,
            rate_ml_s          = rate_ml_s,
            interval_ms        = 50,
            timeout_s          = 30.0,
        )

        if pumped is None:
            print("  [FAIL] timed out waiting for MSG_FUEL_PUMPED")
            return {
                "name":   name,
                "passed": False,
                "reason": "timeout",
            }

        rv = pumped.get("vol_lx1000",      0)
        rt = pumped.get("total_pricex100", 0)

        vol_ok   = abs(rv - target_vol)   <= VOL_TOLERANCE
        total_ok = abs(rt - target_total_x100) <= TOTAL_TOLERANCE
        passed   = vol_ok and total_ok

        tag = "[PASS]" if passed else "[FAIL]"
        print(f"  {tag}")
        print(f"    volume : expected={target_vol}  received={rv}  "
              f"delta={abs(rv - target_vol)}  {'OK' if vol_ok else 'FAIL'}")
        print(f"    total  : expected={target_total_x100}  received={rt}  "
              f"delta={abs(rt - target_total_x100)}  {'OK' if total_ok else 'FAIL'}")

        return {
            "name":          name,
            "passed":        passed,
            "target_vol":    target_vol,
            "target_total":  target_total_x100,
            "received_vol":  rv,
            "received_total": rt,
            "vol_delta":     abs(rv - target_vol),
            "total_delta":   abs(rt - target_total_x100),
        }
