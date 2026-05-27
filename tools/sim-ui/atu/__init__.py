"""
atu — AutomatedTestUnit library for ferp-com firmware testing.

Provides:
  SimLauncher          Start / stop the ferp-com-simulator C++ binary.
  AutomatedTestUnit    High-level facade: connect, pump, monitor, press buttons.

Quick-start
-----------
    from atu import SimLauncher, AutomatedTestUnit, DT_SANKI_6

    launcher = SimLauncher()          # auto-locates binary relative to repo root
    launcher.start()                  # blocks until TCP socket is accepting

    atu = AutomatedTestUnit()
    atu.connect()

    result = atu.pump(nozzle=0).run_transaction(
        display_type    = DT_SANKI_6,
        unit_price_x100 = 42300,       # $4.23 / L
        target_vol_lx1000 = 5_000,     # 5.000 L
    )
    print(result)

    atu.close()
    launcher.stop()
"""

from .atu      import AutomatedTestUnit
from .launcher import SimLauncher

# Re-export display-type constants so test scripts only need to import from atu
from sim_core import (
    DISPLAY_TYPES,
    DT_NONE,
    DT_CENSTAR_6,
    DT_CENSTAR_7,
    DT_CENSTAR_7C,
    DT_HONGYANG,
    DT_WAYNE_6,
    DT_SANKI_6,
    DT_LONGFENG,
)

__all__ = [
    "AutomatedTestUnit",
    "SimLauncher",
    "DISPLAY_TYPES",
    "DT_NONE",
    "DT_CENSTAR_6",
    "DT_CENSTAR_7",
    "DT_CENSTAR_7C",
    "DT_HONGYANG",
    "DT_WAYNE_6",
    "DT_SANKI_6",
    "DT_LONGFENG",
]
