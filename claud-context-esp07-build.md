# distap-esp07 build — context handoff (Mac → Windows)

## Goal

Build the ESP07 (ESP8266) DispTap firmware from source at
`src/sub-modules/ferp-device-firmware/ferp_board/distap-esp07`, because the
`.bin` files currently used for the field ESP07 module are outdated/incompatible
with the new v2 (esp07) product firmware. This mirrors what already exists for
`distap-esp32`.

**Status as of this doc:** on macOS (Apple Silicon), the project now builds
and links to a complete `.elf`/`.bin` with zero errors, using the toolchain
setup and source fixes below. Flashing to real hardware was **not** tested
(no device attached in that session) — that's the next thing to verify.

## Why this needed a completely separate toolchain

`distap-esp32` and the main v2/v3 products use mainline **ESP-IDF**. The ESP07
is an **ESP8266**, built against **ESP8266_RTOS_SDK**
(github.com/espressif/ESP8266_RTOS_SDK) with the **xtensa-lx106-elf** toolchain
— a different SDK and different compiler entirely. They coexist fine
side-by-side (different install directories, different `IDF_PATH` per board).

## What's already done and committed to the repo (carries over automatically)

These are source-level fixes to the project itself — nothing machine-specific,
they'll be present as soon as you pull/copy the repo to the Windows machine.

### 1. New build scripts (mirroring distap-esp32's pattern)
- `distap-esp07/build.py` — `--cleanall/--flashall/--flashapp/--console/--release`
- `distap-esp07/build.sh` — POSIX wrapper with default port/SDK path baked in,
  plus one-time toolchain setup instructions in a header comment
- `distap-esp07/tools/ota-bundle-tools/` — `ota_bundle.py` + three bundle
  creators (`Bootloader`/`Partitions`/`Firmware`) using target names
  `esp07-dt-boot`/`esp07-dt-part`/`esp07-dt-fw`, matching the OTA target table
  in `ferp-com-v2-main/app/app.cpp` and the paths `serial_flasher.cpp` expects
- Root `build.py` — added `--release-dt-esp07`, delegating to the above
  (same pattern as the existing `--release-dt-esp32`)

### 2. Real bugs fixed in the project's CMakeLists.txt files

The project had **never been successfully built via `idf.py`/CMake before** —
only via the legacy `make` flow (confirmed: `components/device/CMakeLists.txt`
had a half-finished, commented-out attempt at porting one of these fixes).
The old Make-based build (`component.mk`) glued all components' include paths
together globally, hiding these gaps; `idf.py`'s CMake build enforces proper
per-component dependency declarations (`REQUIRES`/`PRIV_REQUIRES`), which
surfaced every missing one, one compile-error at a time.

Fixed, in the order the build errors appeared:

| File | Fix | Why |
|---|---|---|
| `components/{censtar,hongyang,sanki,wayn}_pump/CMakeLists.txt`, `components/longfeng/CMakeLists.txt` | added `REQUIRES device` | each `*.c` includes `settings.h` (private use), and each component's own public header (e.g. `wayn_6_digit.h`) includes `device.h` (public use) — needs `REQUIRES`, not `PRIV_REQUIRES`, so consumers get it transitively |
| `components/device/CMakeLists.txt` | added `PRIV_REQUIRES json spiffs censtar_pump hongyang_pump longfeng sanki_pump wayn_pump` | `settings.c` includes `cJSON.h` (→ `json` component); `spiff_mount.c` includes `esp_spiffs.h` (→ `spiffs` component); `device.c` includes all five pump/longfeng headers directly. This creates a two-way dependency (pumps require device, device requires pumps) — confirmed this is fine with `PRIV_REQUIRES` on both sides (no circular-dependency error from CMake) |
| `components/device/CMakeLists.txt` | completed the existing commented-out `set_source_files_properties` block, adding `PROJECT_VER`, `PROJECT_NAME`, `PROJECT_TIME`, `PROJECT_DATE` as compile definitions for `device.c` | these were previously injected via `component.mk`'s `CFLAGS` for the Make-only build; `idf.py`'s CMake build never reads `component.mk`, so `device.c` (which sends these in `ID_CMD_DEV_VERSION`/`_PROJ_NAME`/`_TIMEDATE` responses) failed with "undeclared identifier" |
| `main/CMakeLists.txt` | added `PRIV_REQUIRES device json pthread`; added the same `set_source_files_properties` for `PROJECT_NAME` | `main.c` directly includes `spiff_mount.h`/`device.h`/`settings.h`/`cJSON.h` and logs `PROJECT_NAME` at startup — same root causes as above. `pthread` is the odd one: nothing in `main.c` uses it directly, but the C++ static-local-variable init guard in `libstdc++` (pulled in by the `wear_levelling` component, which is C++) needs `pthread_cond_init/broadcast/wait` at **link** time. `libpthread.a` gets built either way but only gets linked into the final `.elf` if something in `main`'s dependency closure requires it — nothing did, so the link failed with `undefined reference to pthread_cond_*` even though the implementation existed in the tree. Declaring it on `main` fixed it. |

If you rebuild and hit a **new** "fatal error: X.h: No such file or directory"
or "undefined reference" during compile/link, it's the same pattern — find
which component provides that header (`find ~/DATA/esp/ESP8266_RTOS_SDK/components -iname X.h`, or search the project's own `components/`), and add
`REQUIRES`/`PRIV_REQUIRES <that component>` to the CMakeLists.txt of whoever
`#include`s it (check whether it's used only in a `.c` file → `PRIV_REQUIRES`,
or also re-exposed in that component's own public `include/*.h` → `REQUIRES`).

### 3. `build.py` output-path correction (already fixed, just noting why)

The project's own `README.md` documents the **Make** build's output layout
(`build/rtos_dis_tap_esp07.bin`, `build/partitions_table.bin` flat,
`build/bootloader/bootloader.bin`). The **CMake/idf.py** build's actual output
differs: the partition table lands at `build/partition_table/partition-table.bin`
(same convention as `distap-esp32`), not flat. `build.py`'s `--release` path
copying already accounts for this — verified against a real build output tree.

## Toolchain setup done on the Mac (some of this is Mac-specific — read the notes)

1. **Cloned the SDK**: `release/v3.4` branch, to `~/DATA/esp/ESP8266_RTOS_SDK`
   (this exact path is hardcoded as the default in `build.sh` — update it if
   your Windows checkout lives elsewhere, or pass `--idf-path`).

2. **Ran `install.sh`** (downloads the `xtensa-lx106-elf` toolchain + sets up
   a dedicated Python venv). On Windows this SDK version does **not** ship an
   `install.bat`/`export.bat` — see the "Windows-specific" section below,
   this is the biggest divergence from the esp32 boards' setup.

3. **Mac-only fixes below — likely NOT needed on Windows, but keep in mind
   if you hit the equivalent symptom:**

   - **Apple Silicon toolchain platform mapping**: `tools/idf_tools.py`'s
     platform map had no `Darwin-arm64` entry, so it refused to run at all on
     Apple Silicon Macs (the actual toolchain listed for macOS is an x86_64
     build from 2020 — works fine under Rosetta 2 once the platform map
     recognizes arm64). **Not applicable on Windows.**

   - **Missing `python` binary**: this SDK's `export.sh`/`idf_tools.py` call
     bare `python` (not `python3`) internally; modern macOS ships only
     `python3`. Baked a fix directly into `distap-esp07/build.py`'s
     `setup_idf()` — it auto-creates a `python -> python3` shim in
     `~/.espressif/python-shim` and prepends it to PATH just for the SDK
     subprocess call, no global changes. **On Windows, check `python --version`
     first — the official python.org / MS Store installer usually provides
     `python.exe` directly, so this may be a non-issue.** If you do hit
     `'python' is not recognized`, the same shim idea applies (a `python.exe`
     shim/symlink pointing at `python3.exe` or your actual interpreter).

   - **`pkg_resources` missing**: the venv's freshly-installed `setuptools`
     no longer bundles `pkg_resources` by default (recent setuptools dropped
     it). Fixed with, inside the SDK's dedicated venv:
     ```
     <venv>/bin/python -m pip install "setuptools<81"
     ```
     This is a pip/package-version issue, **not OS-specific** — you may hit
     this on Windows too if a similarly new setuptools gets installed.

   - **Shallow-clone `git describe` failure**: I initially did a shallow
     clone (`--depth 1`) to save time; `idf_tools.py`'s Python-venv-naming
     logic calls `git describe --tags`, which needs real tag/commit history.
     Fixed by fetching tags and then `git fetch --unshallow`. **Avoid this
     entirely on Windows — just do a normal (non-shallow) clone:**
     ```
     git clone -b release/v3.4 --recursive https://github.com/espressif/ESP8266_RTOS_SDK.git
     ```

   - **CMake too new for vendored `mbedtls`**: modern CMake (4.x, and recent
     3.31+) hard-errors on `cmake_minimum_required(VERSION < 3.5)`, which the
     SDK's vendored `components/mbedtls/mbedtls_v2/mbedtls/CMakeLists.txt`
     still declares. **This IS relevant on Windows** if you have a recent
     CMake installed. Fix: set the environment variable before building —
     ```
     set CMAKE_POLICY_VERSION_MINIMUM=3.5
     ```
     (or export it, if running from Git Bash/MSYS2). I did not bake this into
     `build.py` since it's a genuine CMake-version-dependent workaround, not
     something safe to silently patch project-wide — if you hit the same
     `mbedtls_v2/mbedtls/CMakeLists.txt` error, this is the fix.

## Windows-specific: this SDK has no native cmd/PowerShell tooling

Unlike mainline ESP-IDF (which ships `export.bat`/`install.bat`/`export.ps1`
alongside `export.sh`), **ESP8266_RTOS_SDK release/v3.4 only has `export.sh`/
`install.sh`** — no Windows-native equivalents. This is also why the project's
own `README.md` documents an MSYS2-based Windows setup
(`C:\msys32\mingw32.exe`, then work from that bash shell).

**Recommendation: don't write a `build.win.bat` for this board** — there's no
`export.bat` for it to call, so it can't follow the same pattern as the root
`build.win.bat` (which activates mainline ESP-IDF's real `export.bat` before
invoking Python). The two realistic options:

1. **(Recommended, matches the project's own documented setup)** Install
   MSYS2 or Git Bash, clone/install the SDK from that shell per the steps
   above, then just run the existing `distap-esp07/build.sh` from that same
   bash shell — it's already POSIX-correct and needs no changes.
2. Write a native `.bat` that manually replicates what `export.sh` does
   (parse `tools.json`, compute toolchain/venv paths, set `PATH` directly) —
   possible but meaningfully more work and fragile across SDK updates; only
   worth it if MSYS2/Git Bash is genuinely not an option.

If you want to go the `.bat` route anyway, say so in the new thread and share
this file — the CMake variable name, component-dependency fixes, and output
paths above are all you need; only the "how do I get `idf.py` onto PATH"
mechanics would need to be reworked for native Windows.

## Quick-start on the new machine (assuming MSYS2/Git Bash route)

```bash
# 1. Clone the SDK (full clone, not shallow)
git clone -b release/v3.4 --recursive \
  https://github.com/espressif/ESP8266_RTOS_SDK.git \
  ~/DATA/esp/ESP8266_RTOS_SDK   # or wherever you prefer

# 2. Install toolchain + python env
cd ~/DATA/esp/ESP8266_RTOS_SDK
./install.sh

# 3. If pkg_resources import fails when you first source export.sh:
<path-shown-by-install.sh-for-the-venv>/bin/python -m pip install "setuptools<81"

# 4. Update distap-esp07/build.sh's IDF_PATH if your SDK checkout path differs

# 5. Build (set the CMake policy var first if you hit the mbedtls error)
cd <repo>/src/sub-modules/ferp-device-firmware/ferp_board/distap-esp07
export CMAKE_POLICY_VERSION_MINIMUM=3.5
./build.sh --cleanall --serial-port <your COM/tty port>
```

## Files touched this session (all under `distap-esp07/`)

New:
- `build.py`, `build.sh`
- `tools/ota-bundle-tools/ota_bundle.py`
- `tools/ota-bundle-tools/OtaBundleCreate-DTEsp07_{Bootloader,Partitions,Firmware}.py`
- `sdkconfig` (auto-generated by the first `idf.py` configure — harmless,
  `distap-esp32` also commits its generated `sdkconfig`, so this is consistent)

Modified:
- `components/censtar_pump/CMakeLists.txt`
- `components/hongyang_pump/CMakeLists.txt`
- `components/sanki_pump/CMakeLists.txt`
- `components/wayn_pump/CMakeLists.txt`
- `components/longfeng/CMakeLists.txt`
- `components/device/CMakeLists.txt`
- `main/CMakeLists.txt`

Also modified (outside the repo, on this Mac only — **not relevant to
Windows**): `~/DATA/esp/ESP8266_RTOS_SDK/tools/idf_tools.py` (added the
`Darwin-arm64` platform mapping entry). This lives in the SDK checkout, not
the project repo, so it won't appear in `git status` and won't carry over.

## Next step

Get the toolchain installed on the Windows machine, run `build.sh --cleanall`
(via MSYS2/Git Bash) with a real device attached, and confirm the flash +
first boot succeed. If a new compile/link error shows up that isn't listed
above, it's almost certainly the same "missing REQUIRES" pattern — see the
table above for the diagnostic approach.
