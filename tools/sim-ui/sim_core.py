"""
sim_core.py — Headless simulator connection and emulation units.

Shared by:
  - sim_ui.py          (interactive Tkinter UI)
  - run_tests.py / test scripts (automated testing)

Architecture (from AutomatedTesting.md):
  Simulator (C++) ── TCP backdoor socket ──► SimConnection
                                                │
                                          SimClient
                                         /         \\
                              PumpEmulator     OutputMonitor
                         (Pumping Emulation)  (Device Output Monitoring)

Classes
-------
SimConnection   Low-level TCP connection to the firmware simulator backdoor socket.
                Sends JSON commands as newline-delimited strings.
                Receives JSON events on a background daemon thread.

SimClient       Wraps SimConnection with per-message-id subscription callbacks
                and blocking wait_for() — the contract point for both the UI and tests.

PumpEmulator    Pumping Emulation Unit.
                Drives one nozzle through a complete transaction without Tkinter:
                  nozzle_up → repeated SIM_DISTAP_FRAME ticks → nozzle_down
                Handles display-type-specific frame encoding (Sanki 6-digit wrapping, etc.)

OutputMonitor   Device Output Monitoring Unit.
                Subscribes to all relevant output messages and records them for
                post-transaction assertions in test cases.

Constants
---------
DISPLAY_TYPES   Map of human-readable name → firmware display_type_t integer value.
DT_*            Convenience integer aliases for each display type.
"""

import json
import queue
import socket
import threading
import time

# ── Display type constants ────────────────────────────────────────────────────
# Mirrors firmware display_types.h and was previously defined in nozzle_widget.py.
# Both the UI (nozzle_widget.py) and the test framework import from here.

DISPLAY_TYPES: dict[str, int] = {
    "DIS_NONE (0)":           0,
    "CENSTAR 6-digit (1)":    1,
    "CENSTAR 7-digit (2)":    2,
    "CENSTAR 7-digit CS (3)": 3,
    "HONGYANG 8-digit (4)":   4,
    "WAYNE 6-digit (5)":      5,
    "SANKI 6-digit (6)":      6,
    "LONGFENG 8-digit (7)":   7,
}

DT_NONE       = 0
DT_CENSTAR_6  = 1
DT_CENSTAR_7  = 2
DT_CENSTAR_7C = 3
DT_HONGYANG   = 4
DT_WAYNE_6    = 5
DT_SANKI_6    = 6
DT_LONGFENG   = 7

# ── Pump ramp constants ───────────────────────────────────────────────────────
# Every transaction has a ramp-up zone (first _RAMP_VOL_ML mL) and a ramp-down
# zone (last _RAMP_VOL_ML mL) that run at _RAMP_FRACTION of the full pump rate.
# _RAMP_RATE_CAP_ML_S caps the ramp rate so the smallest test case (~1 182 mL)
# always satisfies the firmware state-machine guard: elapsed >= 4 s OR
# increment_count >= 10 between nozzle UP and nozzle DOWN.
_RAMP_VOL_ML        = 1_000   # mL — size of each ramp zone (1 litre)
_RAMP_FRACTION      = 0.25    # ramp rate = 25 % of the configured full rate
_RAMP_RATE_CAP_ML_S = 295.0   # mL/s — maximum ramp rate


# ── SimConnection ─────────────────────────────────────────────────────────────

class SimConnection:
    """
    TCP connection to the ferp-com simulator backdoor socket.

    Moved here from sim_ui.py so that both the interactive UI and headless
    test scripts share the same implementation.

    - connect() opens the socket and starts a background reader thread.
    - send()    writes a JSON object as a newline-terminated string.
    - _reader() puts every received JSON object into rx_queue.
    - close()   shuts down the socket and the reader thread.
    """

    def __init__(self, host: str, port: int, rx_queue: queue.Queue):
        self._host      = host
        self._port      = port
        self._q         = rx_queue
        self._sock      = None
        self._running   = False
        self._send_lock = threading.Lock()

    def connect(self) -> bool:
        try:
            self._sock = socket.create_connection((self._host, self._port), timeout=5)
            self._sock.settimeout(None)
            self._running = True
            t = threading.Thread(target=self._reader, daemon=True)
            t.start()
            return True
        except OSError as e:
            self._q.put({"id": "_SIM_ERROR", "data": {"msg": str(e)}})
            return False

    def send(self, obj: dict):
        if self._sock is None:
            return
        line = json.dumps(obj) + "\n"
        with self._send_lock:
            try:
                self._sock.sendall(line.encode())
            except OSError:
                pass

    def _reader(self):
        buf = b""
        while self._running:
            try:
                chunk = self._sock.recv(4096)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.strip()
                    if line:
                        try:
                            obj = json.loads(line.decode())
                            self._q.put(obj)
                        except json.JSONDecodeError:
                            # Not JSON — treat as plain log line
                            self._q.put({"id": "_LOG", "data": {"text": line.decode()}})
            except OSError:
                break
        self._q.put({"id": "_SIM_DISCONNECTED", "data": {}})

    def close(self):
        self._running = False
        if self._sock:
            try:
                self._sock.close()
            except OSError:
                pass


# ── SimClient ─────────────────────────────────────────────────────────────────

class SimClient:
    """
    Headless simulator client — Python Connection to Firmware Simulator Backdoor.

    Wraps SimConnection with:
      - subscribe(msg_id, callback)  — register callback(data, ts) for a message ID
      - wait_for(msg_id, ...)        — block until a matching message arrives
      - send(obj)                    — forward command to the simulator

    All callbacks are dispatched on a dedicated background thread so the caller
    (test script or UI) is never blocked by the receive loop.

    Thread safety: _subs dict is protected by _subs_lock.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 9000):
        self._rx_queue  = queue.Queue()
        self._conn      = SimConnection(host, port, self._rx_queue)
        self._subs: dict[str, list] = {}
        self._subs_lock = threading.Lock()
        self._running   = False
        self._thread: threading.Thread | None = None

    # ── Connection lifecycle ──────────────────────────────────────────────────

    def connect(self) -> bool:
        ok = self._conn.connect()
        if ok:
            self._running = True
            self._thread  = threading.Thread(target=self._dispatch_loop, daemon=True)
            self._thread.start()
        return ok

    def close(self):
        self._running = False
        self._conn.close()

    # ── Outbound ──────────────────────────────────────────────────────────────

    def send(self, obj: dict):
        self._conn.send(obj)

    # ── Inbound subscription API ──────────────────────────────────────────────

    def subscribe(self, msg_id: str, callback) -> None:
        """Register callback(data: dict, ts: int) for the given message ID."""
        with self._subs_lock:
            self._subs.setdefault(msg_id, []).append(callback)

    def unsubscribe(self, msg_id: str, callback) -> None:
        with self._subs_lock:
            lst = self._subs.get(msg_id, [])
            if callback in lst:
                lst.remove(callback)

    def wait_for(
        self,
        msg_id: str,
        timeout: float = 10.0,
        predicate=None,
    ) -> dict | None:
        """
        Block until a message with msg_id arrives.

        predicate(data: dict) → bool  — optional filter; default accepts any.
        Returns the matching data dict, or None on timeout.

        Subscribe BEFORE triggering the action that causes the message to avoid
        a race between the trigger and the subscription.
        """
        ev     = threading.Event()
        result = [None]

        def handler(data, ts):
            if predicate is None or predicate(data):
                result[0] = data
                ev.set()

        self.subscribe(msg_id, handler)
        ev.wait(timeout=timeout)
        self.unsubscribe(msg_id, handler)
        return result[0]

    # ── Internal dispatch loop ────────────────────────────────────────────────

    def _dispatch_loop(self):
        while self._running:
            try:
                obj    = self._rx_queue.get(timeout=0.1)
                msg_id = obj.get("id", "")
                data   = obj.get("data", {})
                ts     = obj.get("ts", 0)
                with self._subs_lock:
                    cbs = list(self._subs.get(msg_id, []))
                for cb in cbs:
                    try:
                        cb(data, ts)
                    except Exception:
                        pass
            except queue.Empty:
                pass


# ── Frame encoding helpers ────────────────────────────────────────────────────

def encode_frame_sanki6(real_vol_lx1000: int, unit_pricex100: int) -> tuple[int, int]:
    """
    Encode a pump frame as a real Sanki 6-digit display would send it.

    Sanki 6-digit hardware:
      Volume display — 6 digits (DDDD.DD): max 9999.99 L → vol_lx1000 = 9 999 990
      Price  display — 6 digits (DDDD.DD): max $9999.99  → total_pricex100 = 999 999

    When the real accumulated value exceeds the 6-digit max, the display transmits
    only the lower digits (natural integer overflow on the display hardware).
    The firmware (sanki6_process_data) detects the gap between
    unit_price × volume and displayed_total and adds back the missing multiples.

    Parameters
    ----------
    real_vol_lx1000   Actual accumulated volume (mL, i.e. vol × 1000).
    unit_pricex100    Unit price per litre × 100 (e.g. 42300 = $4.23/L).

    Returns
    -------
    (disp_vol_lx1000, disp_total_pricex100)  — values to send in SIM_DISTAP_FRAME.
    """
    real_total_x100 = (unit_pricex100 * real_vol_lx1000) // 1000
    disp_vol   = real_vol_lx1000  % 10_000_000   # 7-digit rollover (6-digit display)
    disp_total = real_total_x100  %  1_000_000   # 6-digit rollover
    return disp_vol, disp_total


def encode_frame_censtar6(real_vol_lx1000: int, unit_pricex100: int) -> tuple[int, int]:
    """
    Encode a pump frame as a Censtar 6-digit display would send it.

    Censtar 6-digit hardware sends frame values in x10 (1 decimal place).
    The firmware (fuel_types_from_frame, DIS_CENSTAR_6_DIGIT case) multiplies
    both unit_price and total_price by 10 to produce the internal x100 values.

      Volume display — DDDDD.D: max 99999.9 L → vol_lx1000 wraps at 100,000,000
      Price  display — frame value in x10; firmware ×10 → internal x100.
                       Wraps at 100,000 in x10 (≡ 1,000,000 in x100).

    The firmware (censtar6_process_data branch-2) correction:
      corrected_x100 = displayed_x100 + (expected_x100 // 1M) × 1M
    where displayed_x100 = frame_total × 10.  For this to recover real_total_x100,
    frame_total must carry real_total_x100 % 1M without the last decimal digit,
    i.e. wrap at 100K in x10.  Residual error ≤ 9 in x100 (≤ $0.09).

    Parameters
    ----------
    real_vol_lx1000   Actual accumulated volume (mL, i.e. vol × 1000).
    unit_pricex100    Unit price per litre × 100 (e.g. 42300 = $4.23/L).

    Returns
    -------
    (disp_vol_lx1000, disp_total_x10)  — values to send in SIM_DISTAP_FRAME.
    """
    # Censtar 6-digit frame values are in x10 (1 decimal place).
    # The firmware (fuel_types_from_frame) multiplies both unit_price and
    # total_price by 10 to recover the internal x100 representation.
    #
    # For branch-2 correction to work:
    #   displayed_x100 = disp_total_x10 * 10  must equal  real_total_x100 % 1_000_000
    #   ⟹  disp_total_x10 = (real_total_x100 // 10) % 100_000
    #                      = real_total_x10 % 100_000
    # Max rounding error after correction: real_total_x100 % 10  ∈  {0..9}.
    real_total_x10 = (unit_pricex100 * real_vol_lx1000) // 10_000
    disp_vol   = real_vol_lx1000 % 100_000_000   # DDDDD.D vol: wraps at 100 000.000 L
    disp_total = real_total_x10  %     100_000   # x10 format: 100K wrap ≡ 1M in x100
    return disp_vol, disp_total


def encode_frame_passthrough(real_vol_lx1000: int, unit_pricex100: int) -> tuple[int, int]:
    """No-wrap encoding — sends the raw accumulated values (Censtar 7-digit, HongYang, etc.)."""
    total_x100 = (unit_pricex100 * real_vol_lx1000) // 1000
    return real_vol_lx1000, total_x100


def encode_frame_censtar7(real_vol_lx1000: int, unit_pricex100: int) -> tuple[int, int]:
        """
        Encode Censtar 7-digit frame values.

        In the simulator path, ModuleFuel expects normalized units:
            - unit_price in x100
            - total_price in x100
            - volume in x1000

        Censtar 7 has an extra integer digit vs 6-digit families, so total/volume
        rollover is modeled at 10x the 6-digit boundary.
        """
        real_total_x100 = (unit_pricex100 * real_vol_lx1000) // 1000
        disp_vol = real_vol_lx1000 % 100_000_000   # 7-digit style volume window
        disp_total = real_total_x100 % 10_000_000  # 7-digit style price window
        return disp_vol, disp_total


def encode_frame_hongyang8(real_vol_lx1000: int, unit_pricex100: int) -> tuple[int, int]:
        """
        Encode Hongyang 8-digit frame values.

        Hongyang is treated as normalized x100/x1000 in the simulator path.  Keep
        two decimal digits for price and three decimal digits for volume.
        """
        real_total_x100 = (unit_pricex100 * real_vol_lx1000) // 1000
        disp_vol = real_vol_lx1000 % 1_000_000_000
        disp_total = real_total_x100 % 100_000_000
        return disp_vol, disp_total


def encode_frame_wayne6(real_vol_lx1000: int, unit_pricex100: int) -> tuple[int, int]:
        """
        Encode Wayne 6-digit frame values.

        Wayne hardware has different decimal placement, but in this simulator path
        ModuleFuel receives normalized values (x100 price, x1000 volume) from
        SIM_DISTAP_FRAME. Keep a dedicated encoder so Wayne-specific rules can be
        adjusted independently later if the DT-normalization contract changes.
        """
        real_total_x100 = (unit_pricex100 * real_vol_lx1000) // 1000
        disp_vol = real_vol_lx1000 % 10_000_000
        disp_total = real_total_x100 % 10_000_000
        return disp_vol, disp_total


# Per-display-type frame encoder registry (extend as new types are verified)
_FRAME_ENCODERS = {
    DT_SANKI_6:   encode_frame_sanki6,
    DT_CENSTAR_7: encode_frame_censtar7,
    DT_CENSTAR_6: encode_frame_censtar6,
    DT_WAYNE_6:   encode_frame_wayne6,
    DT_HONGYANG:  encode_frame_hongyang8,
    DT_LONGFENG:  encode_frame_passthrough,
}


# ── PumpEmulator ──────────────────────────────────────────────────────────────

class PumpEmulator:
    """
    Pumping Emulation Unit.

    Drives one nozzle through a complete pump transaction using only threading
    (no Tkinter), so it can run from headless test scripts.

    Transaction sequence
    --------------------
    1. SIM_NOZZLE_INPUT  active=True   (nozzle UP)
    2. Repeated SIM_DISTAP_FRAME  flags=1  at the specified rate
       — frames are encoded via the display-type-specific encoder
    3. SIM_DISTAP_FRAME  flags=0  (pump stopped)
    4. SIM_NOZZLE_INPUT  active=False  (nozzle DOWN)
    5. Wait for MSG_FUEL_PUMPED on this nozzle

    Returns the MSG_FUEL_PUMPED data dict, or None on timeout.
    """

    # Recommended Sanki unit price (matches hardware emulator: $4.23/L × 10000 format)
    SANKI6_DEFAULT_UNIT_PRICE = 42300

    def __init__(self, client: SimClient, nozzle_idx: int):
        self._client     = client
        self._nozzle_idx = nozzle_idx

    # ── Public interface ──────────────────────────────────────────────────────

    def run_transaction(
        self,
        display_type:      int,
        unit_price_x100:   int,
        target_vol_lx1000: int,
        rate_ml_s:         float = 500.0,
        interval_ms:       int   = 100,
        timeout_s:         float = 60.0,
        pause_after_fraction: float | None = None,
        pause_duration_s: float = 0.0,
    ) -> dict | None:
        """
        Run a complete pump transaction and return MSG_FUEL_PUMPED data.

        Parameters
        ----------
        display_type       DT_SANKI_6, DT_CENSTAR_7, … (see DT_* constants)
        unit_price_x100    Unit price per litre × 100.  For Sanki use 42300 ($4.23/L).
        target_vol_lx1000  Target volume in mL (vol × 1000).  e.g. 5000 = 5.000 L.
        rate_ml_s          Simulated pump rate in mL/s.
        interval_ms        Frame send interval in milliseconds.
        timeout_s          Maximum time to wait for MSG_FUEL_PUMPED after pump stop.
        pause_after_fraction Optional fraction of target volume where pumping
                   pauses temporarily while nozzle stays up.
        pause_duration_s   Duration of the pause before pumping resumes.

        Returns
        -------
        MSG_FUEL_PUMPED data dict or None on timeout.
        """
        encode = _FRAME_ENCODERS.get(display_type, encode_frame_passthrough)

        # ── Subscribe BEFORE starting so we never miss the event ─────────────
        pump_done   = threading.Event()
        pumped_data = [None]

        def on_pumped(data, ts):
            if data.get("idx", -1) == self._nozzle_idx:
                print(f"  [DBG] on_pumped fired: idx={data.get('idx')} vol={data.get('vol_lx1000')}", flush=True)
                pumped_data[0] = data
                pump_done.set()

        disconnected_during_tx = threading.Event()
        def _on_disc_tx(data, ts):
            disconnected_during_tx.set()

        self._client.subscribe("MSG_FUEL_PUMPED", on_pumped)
        self._client.subscribe("_SIM_DISCONNECTED", _on_disc_tx)

        try:
            # 1. Nozzle UP
            self._client.send({
                "cmd":    "SIM_NOZZLE_INPUT",
                "nozzle": self._nozzle_idx,
                "active": True,
            })
            print(f"  [DBG] nozzle {self._nozzle_idx} UP sent", flush=True)
            time.sleep(0.15)

            # 2. Stream pump frames — ramp-up / full-rate / ramp-down
            # First and last _RAMP_VOL_ML mL run at 25 % of rate_ml_s.
            # The ramp rate is capped at _RAMP_RATE_CAP_ML_S so small pumps
            # stay up long enough for the firmware state machine.
            ramp_vol_ml = min(_RAMP_VOL_ML, target_vol_lx1000 // 2)
            ramp_rate   = min(rate_ml_s * _RAMP_FRACTION, _RAMP_RATE_CAP_ML_S)
            accumulated_ml = 0.0
            pause_done = False
            pause_target_ml = None
            if pause_after_fraction is not None:
                pause_target_ml = int(round(target_vol_lx1000 * pause_after_fraction))
                if pause_target_ml <= 0 or pause_target_ml >= target_vol_lx1000:
                    pause_target_ml = None

            while accumulated_ml < target_vol_lx1000:
                in_ramp = (
                    accumulated_ml < ramp_vol_ml or
                    accumulated_ml >= target_vol_lx1000 - ramp_vol_ml
                )
                eff_rate          = ramp_rate if in_ramp else rate_ml_s
                rate_per_interval = eff_rate * (interval_ms / 1000.0)
                accumulated_ml   += rate_per_interval
                vol = min(int(round(accumulated_ml)), target_vol_lx1000)
                disp_vol, disp_total = encode(vol, unit_price_x100)
                self._send_frame(display_type, unit_price_x100,
                                 disp_vol, disp_total, flags=1)

                if (not pause_done and pause_target_ml is not None and vol >= pause_target_ml):
                    self._send_frame(display_type, unit_price_x100,
                                     disp_vol, disp_total, flags=0)
                    print(
                        f"  [DBG] pausing at {vol} mL for {pause_duration_s:.1f}s with nozzle up",
                        flush=True,
                    )
                    time.sleep(pause_duration_s)
                    pause_done = True

                time.sleep(interval_ms / 1000.0)

            # 3. Stop frame (flags=0)
            disp_vol, disp_total = encode(target_vol_lx1000, unit_price_x100)
            self._send_frame(display_type, unit_price_x100,
                             disp_vol, disp_total, flags=0)
            time.sleep(0.15)

            # 4. Nozzle DOWN
            self._client.send({
                "cmd":    "SIM_NOZZLE_INPUT",
                "nozzle": self._nozzle_idx,
                "active": False,
            })
            print(f"  [DBG] nozzle {self._nozzle_idx} DOWN sent; waiting for PUMPED (timeout={timeout_s}s)…", flush=True)

            # 5. Wait for MSG_FUEL_PUMPED
            pump_done.wait(timeout=timeout_s)
            if pumped_data[0] is None:
                disc_flag = " [DISCONNECTED DURING TX!]" if disconnected_during_tx.is_set() else ""
                print(f"  [DBG] TIMEOUT — MSG_FUEL_PUMPED never arrived{disc_flag}", flush=True)
            else:
                print(f"  [DBG] MSG_FUEL_PUMPED received: {pumped_data[0]}", flush=True)
            return pumped_data[0]

        finally:
            self._client.unsubscribe("MSG_FUEL_PUMPED", on_pumped)
            self._client.unsubscribe("_SIM_DISCONNECTED", _on_disc_tx)

    # ── Internal ──────────────────────────────────────────────────────────────

    def _send_frame(
        self,
        display_type:   int,
        unit_price_x100: int,
        vol_lx1000:     int,
        total_pricex100: int,
        flags:          int = 1,
    ):
        # Censtar 6-digit: firmware multiplies frame unit_price and total_price
        # by 10 (fuel_types_from_frame) to recover x100. Send as x10.
        frame_unit_price = (
            unit_price_x100 // 10
            if display_type == DT_CENSTAR_6
            else unit_price_x100
        )
        self._client.send({
            "cmd":          "SIM_DISTAP_FRAME",
            "nozzle":       self._nozzle_idx,
            "display_type": display_type,
            "flags":        flags,
            "error":        0,
            "unit_price":   frame_unit_price,
            "total_price":  total_pricex100,
            "volume_l":     vol_lx1000,
        })


# ── OutputMonitor ─────────────────────────────────────────────────────────────

class OutputMonitor:
    """
    Device Output Monitoring Unit.

    Subscribes to all relevant device output messages and stores them in a
    thread-safe buffer so test cases can assert on what the firmware produced.

    Usage
    -----
    monitor = OutputMonitor(client)
    monitor.start()               # subscribe once at test-suite setup

    # --- run a transaction ---
    monitor.clear()               # wipe log before each test case
    pumped = emulator.run_transaction(...)
    msgs   = monitor.get("MSG_FUEL_PUMPED")
    buzzer = monitor.wait_buzzer_on(timeout_s=3.0)
    """

    # Messages recorded automatically
    MONITORED = (
        "MSG_FUEL_PUMPED",
        "MSG_NOZZLE_STATE",
        "MSG_PRINTER_BTN",
        "SIM_BUZZER",
        "SIM_GPIO_OUT",
        "MSG_CUBESPHERE_STATUS",
        "MSG_OTA_EVENT",
        "MSG_DEFAULT_BTN",
    )

    # Buzzer GPIO identification (matches sim_ui.py _handle_sim_gpio_out)
    _BUZZER_PIN  = 26
    _BUZZER_NAME = "BUZZER"

    def __init__(self, client: SimClient):
        self._client   = client
        self._lock     = threading.Lock()
        self._recorded: dict[str, list[dict]] = {}

    def start(self):
        """Subscribe to all monitored message IDs.  Call once at suite setup."""
        for msg_id in self.MONITORED:
            self._client.subscribe(
                msg_id,
                lambda data, ts, m=msg_id: self._record(m, data),
            )

    def clear(self):
        """Wipe all recorded messages.  Call before each test case."""
        with self._lock:
            self._recorded.clear()

    def get(self, msg_id: str) -> list[dict]:
        """Return a snapshot of all recorded messages for the given ID."""
        with self._lock:
            return list(self._recorded.get(msg_id, []))

    def wait_buzzer_on(self, timeout_s: float = 5.0) -> bool:
        """
        Block until the BUZZER GPIO goes HIGH (buzzer activated by firmware).
        Returns True if the buzzer fired within timeout_s, False otherwise.

        Useful for verifying buzzer patterns after button presses.
        """
        ev = threading.Event()

        def on_gpio(data, ts):
            name  = data.get("name", "")
            pin   = data.get("pin", -1)
            level = data.get("level", 0)
            if (name == self._BUZZER_NAME or pin == self._BUZZER_PIN) and level != 0:
                ev.set()

        self._client.subscribe("SIM_GPIO_OUT", on_gpio)
        triggered = ev.wait(timeout=timeout_s)
        self._client.unsubscribe("SIM_GPIO_OUT", on_gpio)
        return triggered

    # ── Internal ──────────────────────────────────────────────────────────────

    def _record(self, msg_id: str, data: dict):
        with self._lock:
            self._recorded.setdefault(msg_id, []).append(data)
