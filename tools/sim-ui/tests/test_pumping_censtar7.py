"""
test_pumping_censtar7.py — Censtar 7-digit pump configuration for shared pumping tests.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

from atu import AutomatedTestUnit, DT_CENSTAR_7
from tests.test_pumping import CommonPumpingTests, PumpSuiteConfig, COMMON_RANDOM_CASES_X100


CONFIG = PumpSuiteConfig(
    display_type=DT_CENSTAR_7,
    display_name="Censtar 7-digit",
    display_id_label="2",
    test_prefix="censtar7",
    unit_price_x100=42_300,
    nozzle=0,
    pump_rate_ml_s=21_000,
    volume_tolerance=1,
    total_tolerance=200,
    standard_cases=[
        ("below_1k", 50_000, "$500.00"),
        ("below_10k", 500_000, "$5,000.00"),
        ("below_100k", 5_000_000, "$50,000.00"),
        ("below_200k", 9_000_000, "$90,000.00"),
    ],
    pause_resume_total_x100=5_000_000,
    random_cases_x100=COMMON_RANDOM_CASES_X100,
)


class Censtar7PumpingTests(CommonPumpingTests):
    def __init__(self, atu: AutomatedTestUnit, launcher=None):
        super().__init__(atu, CONFIG, launcher=launcher)
