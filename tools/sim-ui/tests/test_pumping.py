"""
test_pumping.py — shared pumping test implementation for all supported pump types.
"""

from __future__ import annotations

from dataclasses import dataclass
import time

from atu import AutomatedTestUnit


@dataclass(frozen=True)
class PumpSuiteConfig:
    display_type: int
    display_name: str
    display_id_label: str
    test_prefix: str
    unit_price_x100: int
    nozzle: int
    pump_rate_ml_s: float
    volume_tolerance: int
    total_tolerance: int
    standard_cases: list[tuple[str, int, str]]
    pause_resume_total_x100: int
    random_cases_x100: list[int]
    enable_pause_resume: bool = True
    enable_consecutive_random: bool = True


COMMON_RANDOM_CASES_X100 = [
    73_400,
    182_500,
    623_700,
    1_240_000,
    2_310_000,
    3_875_000,
    4_210_000,
    5_680_000,
    7_150_000,
    8_400_000,
]


class CommonPumpingTests:
    def __init__(self, atu: AutomatedTestUnit, config: PumpSuiteConfig, launcher=None):
        self._atu = atu
        self._config = config
        self._launcher = launcher

    def run_all(self) -> list[dict]:
        if not self._setup():
            return [{"name": "setup", "passed": False, "reason": "setup failed"}]

        results: list[dict] = []
        for name, target_total, label in self._config.standard_cases:
            results.append(self._run_standard_case(name, target_total, label))

        if self._config.enable_pause_resume:
            results.append(self._run_pause_resume_case())
        if self._config.enable_consecutive_random:
            results.append(self._run_consecutive_random_case())
        return results

    def _setup(self) -> bool:
        if self._launcher is not None:
            print(f"[SETUP] Setting display type = {self._config.display_name} ({self._config.display_type})…")
            self._atu.set_display_type(self._config.display_type)
            time.sleep(0.5)

            print("[SETUP] Rebooting device…")
            ok = self._atu.reboot_and_reconnect(
                launcher=self._launcher,
                wait_for_exit_s=5.0,
                restart_wait_s=8.0,
            )
            if not ok:
                print("[SETUP] ERROR: failed to reconnect after reboot")
                return False
            print("[SETUP] Reconnected after reboot.")
            time.sleep(3.0)
        else:
            print("[SETUP] No launcher — skipping display type config + reboot.")

        print("[SETUP] Reading display type…")
        dt = self._atu.get_display_type(timeout=5.0)
        if dt is None:
            print("[SETUP] WARNING: display type read timed out (continuing)")
        elif dt != self._config.display_type:
            print(f"[SETUP] WARNING: display type = {dt}, expected {self._config.display_type}")
        else:
            print(f"[SETUP] Display type confirmed = {dt} ({self._config.display_name})  OK")
        return True

    def _target_vol(self, target_total_x100: int) -> int:
        return (target_total_x100 * 1000) // self._config.unit_price_x100

    def _run_standard_case(self, name: str, target_total_x100: int, label: str) -> dict:
        target_vol = self._target_vol(target_total_x100)

        print(f"\n[TEST] {self._config.test_prefix}::{name}")
        print(f"       target total  = {label}  (x100 = {target_total_x100})")
        print(f"       target volume = {target_vol / 1000:.3f} L  (lx1000 = {target_vol})")

        pumped = self._run_transaction(target_vol, timeout_s=30.0)
        return self._build_result(name, target_vol, target_total_x100, pumped)

    def _run_pause_resume_case(self) -> dict:
        name = "pause_after_20pct_then_resume"
        target_total_x100 = self._config.pause_resume_total_x100
        target_vol = self._target_vol(target_total_x100)

        print(f"\n[TEST] {self._config.test_prefix}::{name}")
        print(f"       target total  = {target_total_x100 / 100:.2f}  (x100 = {target_total_x100})")
        print(f"       target volume = {target_vol / 1000:.3f} L  (lx1000 = {target_vol})")
        print("       scenario      = pump 20%, wait 10s with nozzle up, then resume")

        pumped = self._run_transaction(
            target_vol,
            timeout_s=45.0,
            pause_after_fraction=0.20,
            pause_duration_s=10.0,
        )
        return self._build_result(name, target_vol, target_total_x100, pumped)

    def _run_consecutive_random_case(self) -> dict:
        name = "consecutive_10_random_transactions"
        random_cases = self._config.random_cases_x100

        print(f"\n[TEST] {self._config.test_prefix}::{name}")
        print("       scenario      = 10 hardcoded transactions with 5s gap")

        for idx, target_total_x100 in enumerate(random_cases, start=1):
            target_vol = self._target_vol(target_total_x100)
            print(f"       subcase {idx:02d}  total={target_total_x100 / 100:.2f}  volume={target_vol / 1000:.3f} L")
            pumped = self._run_transaction(target_vol, timeout_s=30.0)
            result = self._build_result(
                f"{name}_subcase_{idx:02d}",
                target_vol,
                target_total_x100,
                pumped,
                emit_header=False,
            )
            if not result["passed"]:
                return {
                    "name": name,
                    "passed": False,
                    "reason": f"subcase {idx:02d} failed",
                    "failed_subcase": idx,
                    "failed_total": target_total_x100,
                }

            if idx != len(random_cases):
                print("       waiting 5.0 s before next subcase…")
                time.sleep(5.0)

        print("  [PASS]")
        print(f"    all {len(random_cases)} subcases completed successfully")
        return {
            "name": name,
            "passed": True,
            "count": len(random_cases),
        }

    def _run_transaction(
        self,
        target_vol_lx1000: int,
        timeout_s: float,
        pause_after_fraction: float | None = None,
        pause_duration_s: float = 0.0,
    ) -> dict | None:
        self._atu.monitor.clear()
        return self._atu.pump(self._config.nozzle).run_transaction(
            display_type=self._config.display_type,
            unit_price_x100=self._config.unit_price_x100,
            target_vol_lx1000=target_vol_lx1000,
            rate_ml_s=self._config.pump_rate_ml_s,
            interval_ms=50,
            timeout_s=timeout_s,
            pause_after_fraction=pause_after_fraction,
            pause_duration_s=pause_duration_s,
        )

    def _build_result(
        self,
        name: str,
        target_vol: int,
        target_total_x100: int,
        pumped: dict | None,
        emit_header: bool = True,
    ) -> dict:
        if pumped is None:
            if emit_header:
                print("  [FAIL] timed out waiting for MSG_FUEL_PUMPED")
            return {"name": name, "passed": False, "reason": "timeout"}

        received_vol = pumped.get("vol_lx1000", 0)
        received_total = pumped.get("total_pricex100", 0)
        vol_delta = abs(received_vol - target_vol)
        total_delta = abs(received_total - target_total_x100)
        vol_ok = vol_delta <= self._config.volume_tolerance
        total_ok = total_delta <= self._config.total_tolerance
        passed = vol_ok and total_ok

        if emit_header:
            print(f"  {'[PASS]' if passed else '[FAIL]'}")
            print(
                f"    volume : expected={target_vol}  received={received_vol}  "
                f"delta={vol_delta}  {'OK' if vol_ok else 'FAIL'}"
            )
            print(
                f"    total  : expected={target_total_x100}  received={received_total}  "
                f"delta={total_delta}  {'OK' if total_ok else 'FAIL'}"
            )

        return {
            "name": name,
            "passed": passed,
            "target_vol": target_vol,
            "target_total": target_total_x100,
            "received_vol": received_vol,
            "received_total": received_total,
            "vol_delta": vol_delta,
            "total_delta": total_delta,
        }
