"""
atu.py — AutomatedTestUnit: high-level facade for ferp-com firmware testing.

Architecture (from AutomatedTesting.md):

    Automated Test Unit
    {
        Python Connection to Firmware Simulator Backdoor   ← SimClient
          |
        Pumping Emulation Unit                             ← atu.pump(nozzle)
        Device Output Monitoring Unit                      ← atu.monitor
    }

Usage
-----
    atu = AutomatedTestUnit()
    if not atu.connect():
        raise RuntimeError("Cannot connect to simulator")

    atu.monitor.clear()

    result = atu.pump(nozzle=0).run_transaction(
        display_type      = DT_SANKI_6,
        unit_price_x100   = 42300,
        target_vol_lx1000 = 10_000,
    )
    assert result is not None
    assert abs(result["vol_lx1000"] - 10_000) <= 1

    atu.press_button("print1", hold_ms=80)
    assert atu.monitor.wait_buzzer_on(timeout_s=3)

    atu.close()
"""

import threading
import time

from sim_core import SimClient, PumpEmulator, OutputMonitor

# ── Firmware constants (must match app_config.h / hsys_type.h) ───────────────
_CFG_KEY_DISPLAY_TYPE = 0x6001   # uint16_t config key for display_type
_HSYS_TYPE_UINT32     = 0        # HSYS_TYPE_UINT32 = 0 (first enum in hsys_type.h)
_MSG_ID_CONFIG_SET    = 0x0301   # MSG_ID_CONFIG_SET
_MSG_ID_CONFIG_GET_KEY= 0x030D   # MSG_ID_CONFIG_GET_KEY
_MSG_ID_SYSTEM_REBOOT = 0x020B   # MSG_ID_SYSTEM_REBOOT
_MODULE_SIM_BRIDGE_ID = 20       # MODULE_SIM_BRIDGE_ID (app_module_ids.h)


class AutomatedTestUnit:
    """
    High-level test facade.

    Provides:
      connect()            Open TCP connection to the simulator backdoor socket.
      close()              Shut down the connection.

      pump(nozzle)         Returns a PumpEmulator for the given nozzle index.
      monitor              OutputMonitor for asserting on device outputs.
      client               Underlying SimClient (for advanced / direct use).

      press_button(name)   Simulate a button press-and-release.
      wait_for(msg_id)     Block until a firmware message arrives.
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 9000,
        connect_retries: int = 3,
        retry_interval_s: float = 1.0,
    ):
        self._host             = host
        self._port             = port
        self._retries          = connect_retries
        self._retry_interval   = retry_interval_s
        self._client           = SimClient(host, port)
        self._monitor          = OutputMonitor(self._client)
        self._pump_emulators: dict[int, PumpEmulator] = {}

    # ── Connection ────────────────────────────────────────────────────────────

    def connect(self) -> bool:
        """
        Connect to the simulator and start the output monitor.

        Retries up to connect_retries times with retry_interval_s between attempts.
        Returns True on success, False on failure.
        """
        for attempt in range(1, self._retries + 1):
            ok = self._client.connect()
            if ok:
                self._monitor.start()
                return True
            if attempt < self._retries:
                time.sleep(self._retry_interval)
        return False

    def close(self) -> None:
        """Shut down the simulator connection."""
        self._client.close()

    # ── Context manager ───────────────────────────────────────────────────────

    def __enter__(self):
        if not self.connect():
            raise RuntimeError(
                f"AutomatedTestUnit: failed to connect to "
                f"{self._host}:{self._port}"
            )
        return self

    def __exit__(self, *_):
        self.close()

    # ── Properties ────────────────────────────────────────────────────────────

    @property
    def monitor(self) -> OutputMonitor:
        """Device Output Monitoring Unit — records and waits on firmware outputs."""
        return self._monitor

    @property
    def client(self) -> SimClient:
        """Underlying SimClient — for subscriptions, wait_for, or raw sends."""
        return self._client

    # ── Pumping Emulation Unit ────────────────────────────────────────────────

    def pump(self, nozzle: int = 0) -> PumpEmulator:
        """
        Pumping Emulation Unit for the given nozzle.

        A PumpEmulator is created lazily per nozzle and reused across calls.
        The typical pattern is:

            result = atu.pump(0).run_transaction(
                display_type      = DT_SANKI_6,
                unit_price_x100   = 42300,
                target_vol_lx1000 = 5_000,
            )
        """
        if nozzle not in self._pump_emulators:
            self._pump_emulators[nozzle] = PumpEmulator(self._client, nozzle)
        return self._pump_emulators[nozzle]

    # ── Input simulation ──────────────────────────────────────────────────────

    def press_button(self, btn_name: str, hold_ms: int = 80) -> None:
        """
        Simulate a button press followed by release.

        btn_name  One of: "print1", "print2", "default"
        hold_ms   Hold duration in milliseconds (default 80 ms = short press).
                  Use >= 1500 ms for a long press.
        """
        self._client.send({
            "id":   "SIM_BTN",
            "data": {"btn": btn_name, "action": "press"},
        })
        time.sleep(hold_ms / 1000.0)
        self._client.send({
            "id":   "SIM_BTN",
            "data": {"btn": btn_name, "action": "release"},
        })

    def send(self, obj: dict) -> None:
        """Send a raw command dict to the simulator backdoor."""
        self._client.send(obj)

    # ── Convenience wait ──────────────────────────────────────────────────────

    def wait_for(
        self,
        msg_id:    str,
        timeout:   float = 10.0,
        predicate = None,
    ) -> dict | None:
        """
        Block until a firmware message with the given ID arrives.

        predicate(data: dict) → bool  optional filter.
        Returns the data dict, or None on timeout.
        """
        return self._client.wait_for(msg_id, timeout=timeout, predicate=predicate)

    # ── Message injection ─────────────────────────────────────────────────────

    def inject_msg(
        self,
        msg_id:        int,
        payload:       dict,
        src_module_id: int = _MODULE_SIM_BRIDGE_ID,
        dst_module_id: int = 0,
    ) -> None:
        """
        Inject a HSYS message into the firmware via the SIM_MSG_INJECT command.

        Parameters
        ----------
        msg_id         Numeric message ID (e.g. 0x0301 for MsgConfigSet).
        payload        Dict that matches the message's from_json() contract.
        src_module_id  Sender module ID seen by the firmware (default: sim bridge = 20).
        dst_module_id  Receiver module ID for DIRECT messages; 0 for notifications.
        """
        self._client.send({
            "id": "SIM_MSG_INJECT",
            "data": {
                "msg_id":        msg_id,
                "src_module_id": src_module_id,
                "dst_module_id": dst_module_id,
                "payload":       payload,
            },
        })

    # ── Config helpers ────────────────────────────────────────────────────────

    def set_display_type(self, display_type: int) -> None:
        """
        Set the display type in firmware config (persisted to SPIFFS/NVS).

        Uses MsgConfigSet with key=CFG_KEY_DISPLAY_TYPE, type=UINT32.
        Requires a reboot (via reboot_and_reconnect) to take effect in ModuleFuel.
        """
        val_bytes = list(display_type.to_bytes(4, "little"))
        self.inject_msg(_MSG_ID_CONFIG_SET, {
            "key":  _CFG_KEY_DISPLAY_TYPE,
            "type": _HSYS_TYPE_UINT32,
            "size": 4,
            "data": val_bytes,
        })

    def get_display_type(self, timeout: float = 5.0) -> int | None:
        """
        Read the display type config value from the running firmware.

        Sends MsgConfigGetKey; waits for the MSG_CONFIG_VALUE DIRECT response
        forwarded by ModuleSimBridge.

        Returns the uint32 value, or None on timeout.
        """
        ev     = threading.Event()
        result = [None]

        def on_config_value(data, ts):
            if data.get("key") == _CFG_KEY_DISPLAY_TYPE:
                raw = data.get("data", [])
                if len(raw) >= 4:
                    result[0] = (raw[0]
                                 | (raw[1] << 8)
                                 | (raw[2] << 16)
                                 | (raw[3] << 24))
                ev.set()

        self._client.subscribe("MSG_CONFIG_VALUE", on_config_value)
        # src_module_id=sim bridge so the DIRECT response is routed back through it
        self.inject_msg(_MSG_ID_CONFIG_GET_KEY,
                        {"key": _CFG_KEY_DISPLAY_TYPE},
                        src_module_id=_MODULE_SIM_BRIDGE_ID)
        ev.wait(timeout=timeout)
        self._client.unsubscribe("MSG_CONFIG_VALUE", on_config_value)
        return result[0]

    # ── Reboot & reconnect ────────────────────────────────────────────────────

    def reboot_and_reconnect(
        self,
        launcher=None,
        wait_for_exit_s: float = 5.0,
        restart_wait_s:  float = 8.0,
    ) -> bool:
        """
        Trigger a firmware reboot then reconnect the ATU.

        On macOS the simulator calls std::exit(0), so the process terminates.
        The TCP connection drops, we detect this via _SIM_DISCONNECTED, then:
          - if launcher is provided: stop + start the simulator again.
          - otherwise: wait restart_wait_s seconds (external restart expected).

        After restart, a fresh SimClient is created and connected.
        Returns True on successful reconnect, False on failure.
        """
        # Watch for disconnect
        disconnected = threading.Event()

        def on_disconnect(data, ts):
            disconnected.set()

        self._client.subscribe("_SIM_DISCONNECTED", on_disconnect)

        # Send the reboot message (MSG_ID_SYSTEM_REBOOT = 0x020B)
        self.inject_msg(_MSG_ID_SYSTEM_REBOOT, {})

        # Wait for the TCP connection to drop
        disconnected.wait(timeout=wait_for_exit_s)
        self._client.close()

        # Restart the simulator process
        if launcher is not None:
            launcher.stop()
            launcher.start(wait_s=restart_wait_s)
        else:
            time.sleep(restart_wait_s)

        # Rebuild client + monitor (pump emulators recreated lazily)
        self._client  = SimClient(self._host, self._port)
        self._monitor = OutputMonitor(self._client)
        self._pump_emulators.clear()

        return self.connect()
