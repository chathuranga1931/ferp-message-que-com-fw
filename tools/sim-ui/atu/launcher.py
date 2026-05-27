"""
launcher.py — SimLauncher: start and stop the ferp-com-simulator C++ binary.

The simulator binary is expected at:
    {repo_root}/src/product/ferp-com-main/ferp-com-simulator/build/ferp-com-simulator

Where repo_root is auto-detected by walking up from this file's location until
a directory containing "src/product" is found.

The simulator is started with:
    ./build/ferp-com-simulator --ui-port <port>

from the ferp-com-simulator/ source directory so that the SPIFFS / SDCARD
relative paths used by the firmware remain valid.
"""

import os
import signal
import socket
import subprocess
import time

# ── Repo root resolution ──────────────────────────────────────────────────────

def _find_repo_root() -> str:
    """Walk up from this file until we find the repo root (contains src/product)."""
    candidate = os.path.dirname(os.path.abspath(__file__))
    for _ in range(10):
        if os.path.isdir(os.path.join(candidate, "src", "product")):
            return candidate
        parent = os.path.dirname(candidate)
        if parent == candidate:
            break
        candidate = parent
    raise RuntimeError(
        "Cannot locate repo root (no 'src/product' ancestor found). "
        "Set SimLauncher(binary_path=...) explicitly."
    )


# ── SimLauncher ───────────────────────────────────────────────────────────────

class SimLauncher:
    """
    Start and stop the ferp-com-simulator C++ process.

    Parameters
    ----------
    binary_path   Absolute path to the compiled ferp-com-simulator binary.
                  If None, auto-detected from repo root.
    port          --ui-port value (must match AutomatedTestUnit port).  Default 9000.
    extra_args    Additional command-line arguments forwarded to the binary.
    log_output    If True, simulator stdout/stderr is printed.  Default False.
    """

    _DEFAULT_REL = os.path.join(
        "src", "product", "ferp-com-main",
        "ferp-com-simulator", "build", "ferp-com-simulator",
    )
    _DEFAULT_CWD_REL = os.path.join(
        "src", "product", "ferp-com-main", "ferp-com-simulator",
    )

    def __init__(
        self,
        binary_path: str | None = None,
        port:        int         = 9000,
        extra_args:  list[str]   = (),
        log_output:  bool        = False,
    ):
        if binary_path is None:
            repo_root   = _find_repo_root()
            binary_path = os.path.join(repo_root, self._DEFAULT_REL)
            self._cwd   = os.path.join(repo_root, self._DEFAULT_CWD_REL)
        else:
            self._cwd   = os.path.dirname(binary_path)

        self._binary    = binary_path
        self._port      = port
        self._extra     = list(extra_args)
        self._log       = log_output
        self._proc: subprocess.Popen | None = None

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    def start(self, wait_s: float = 8.0) -> None:
        """
        Launch the simulator process and wait until its TCP port is accepting.

        Raises RuntimeError if the binary is not found or the port never opens.
        """
        if not os.path.isfile(self._binary):
            raise RuntimeError(
                f"Simulator binary not found: {self._binary}\n"
                "Build it first: cd {cwd} && ./build.sh".format(cwd=self._cwd)
            )

        cmd = [self._binary, "--ui-port", str(self._port)] + self._extra
        stdout = None if self._log else subprocess.DEVNULL
        stderr = None if self._log else subprocess.DEVNULL

        self._proc = subprocess.Popen(
            cmd,
            cwd=self._cwd,
            stdout=stdout,
            stderr=stderr,
        )

        if not self._wait_for_port(wait_s):
            self.stop()
            raise RuntimeError(
                f"Simulator did not open port {self._port} within {wait_s}s. "
                "Check the binary and its SPIFFS/SDCARD paths."
            )

    def stop(self) -> None:
        """Terminate the simulator process gracefully, then forcefully if needed."""
        if self._proc is None:
            return
        try:
            self._proc.send_signal(signal.SIGTERM)
            self._proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait()
        except ProcessLookupError:
            pass
        finally:
            self._proc = None

    def is_running(self) -> bool:
        return self._proc is not None and self._proc.poll() is None

    # ── Context manager support ───────────────────────────────────────────────

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *_):
        self.stop()

    # ── Internal ──────────────────────────────────────────────────────────────

    def _wait_for_port(self, timeout_s: float) -> bool:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                s = socket.create_connection(("127.0.0.1", self._port), timeout=0.5)
                s.close()
                return True
            except OSError:
                time.sleep(0.25)
        return False
