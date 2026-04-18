# Folder Structure

> **Maintenance note**: Update this file whenever a source file is added, moved, or removed.
> Last updated: 2026-04-18 — renamed `msg_config_get_request` → `msg_config_get` (`MsgConfigGetRequest` → `MsgConfigGet`, `MSG_ID_CONFIG_GET_REQUEST` → `MSG_ID_CONFIG_GET`)

---

## Root

```
ferp-message-library/
├── CMakeLists.txt              # ESP-IDF top-level build (hello-world stub)
├── FOLDERSTRUCTURE.md          # This file
├── PORTING_PLAN.md
├── TODO_messages.md
├── TODO_modules.md
├── arch.drawio                 # Architecture diagram
├── build/                      # ESP-IDF build output (generated, not tracked)
├── documents/                  # Design / reference documents
├── old-app/                    # Legacy application reference (read-only)
├── src/                        # All active source code (see below)
└── tools/                      # Development tooling (see below)
```

---

## src/

```
src/
│
├── app-messages/               # Application-level message definitions (implement IHsysMsg)
│   ├── IHsysMsg.h              # Base interface for all app messages
│   ├── msg_config_get_request.h/.cpp
│   ├── msg_config_get.h/.cpp   # Empty notification — request ModuleConfig to re-publish config
│   ├── msg_config_ready.h/.cpp # Empty notification — config is ready (no payload)
│   ├── msg_config_set.h/.cpp   # Set a config field by key/type/value (uses hsys_type_t)
│   ├── msg_sensor_data.h/.cpp
│   ├── msg_spiffs_ready.h/.cpp # Published by ModuleSpiffs after SPIFFS mounts
│   └── msg_tick_1000ms.h/.cpp
│
├── app-modules/                # Application modules (extend HsysModule)
│   ├── app_msg_ids.h           # ⚠ Duplicate — canonical copy is in src/product/app/
│   ├── app_msg_table.h
│   ├── CMakeLists.txt
│   ├── module_a/
│   │   ├── module_a.h
│   │   └── module_a.cpp
│   ├── module_b/
│   │   ├── module_b.h
│   │   └── module_b.cpp
│   ├── module_config/
│   │   ├── module_config.h     # MODULE_CONFIG_ID = 6
│   │   └── module_config.cpp   # Subscribes SPIFFS_READY → loads JSON → saves → publishes MsgConfigReady
│   ├── module_spiffs/
│   │   ├── module_spiffs.h     # MODULE_SPIFFS_ID = 5
│   │   └── module_spiffs.cpp   # pre_init: mount; post_init: publish MsgSpiffsReady
│   ├── module_sysmon/
│   │   ├── module_sysmon.h
│   │   └── module_sysmon.cpp
│   └── ticker/
│       ├── ticker.h
│       └── ticker.cpp
│
├── app-pheripherals/           # App-level peripheral wrappers (mutex + error handling over PAL)
│   ├── app_spiffs.h
│   └── app_spiffs.cpp          # Mutex-protected SPIFFS read/write/init
│
├── product/                    # Product-specific configurations and builds
│   ├── app/
│   │   ├── app.h               # extern "C": app_init, app_run, app_config_get_handle, app_config_get_table
│   │   ├── app.cpp             # Owns _app_config; defines app_config_get_handle()
│   │   ├── app_config.h        # app_config_t struct — application-specific config data model
│   │   ├── app_msg_ids.h       # Canonical message ID enum (MSG_ID_SPIFFS_READY, MSG_ID_CONFIG_READY, …)
│   │   ├── user_config.h
│   │   └── CMakeLists.txt
│   ├── ferp-com-esp32-idf/     # ESP32 IDF target build
│   │   ├── CMakeLists.txt
│   │   ├── sdkconfig
│   │   └── main/
│   │       ├── CMakeLists.txt
│   │       └── main.cpp
│   └── ferp-com-simulator/     # macOS simulator target build
│       ├── CMakeLists.txt      # Defines all cmake libraries: pal_mac, app_*, sim_bridge, ferp-com-simulator
│       ├── main/
│       │   └── main.cpp        # Entry point: chdir to source dir, pool/module/task tables + main()
│       ├── sim_bridge/
│       │   ├── module_sim_bridge.h   # SIM_BRIDGE_MODULE_ID; TCP socket bridge to Python UI
│       │   └── module_sim_bridge.cpp
│       └── SPIFFS/             # Emulated SPIFFS partition (simulator only)
│           └── spiffs/
│               └── Configs/
│                   └── DeviceConfigs.json   # Device config file (read/written at runtime)
│
└── sub-modules/                # Git submodules — shared libraries
    ├── hsys-framework/         # Core HSYS publish-subscribe messaging framework
    │   ├── CMakeLists.txt
    │   ├── hsys_config.h
    │   ├── hsys_fw_config.h
    │   ├── hsys_module.h/.cpp  # HsysModule base class (pre_init, init, post_init, on_msg_received)
    │   ├── hsys_msg.h/.cpp     # Message bus (publish, subscribe)
    │   ├── hsys_pool.h/.cpp    # Memory pool
    │   ├── hsys_task_mgr.h/.cpp
    │   └── hsys_types.h
    ├── hsys-os/                # OS abstraction layer
    │   ├── CMakeLists.txt
    │   ├── hsys_event.h / hsys_mutex.h / hsys_queue.h
    │   ├── hsys_semaphore.h / hsys_soft_timer.h / hsys_task.h / hsys_task_append.h
    │   ├── free_rtos/          # FreeRTOS implementations
    │   │   └── hsys_*.cpp
    │   └── mac_os/             # macOS / POSIX implementations
    │       └── hsys_*.cpp
    ├── middleware/             # Shared middleware
    │   ├── hsys_config.h/.cpp  # Config load/save engine (ESP-IDF / ArduinoJson version)
    │   └── list-manager/
    │       ├── list_manager.h/.cpp
    │       └── storage.h
    ├── pal/                    # Platform Abstraction Layer
    │   ├── CMakeLists.txt
    │   ├── pal_*.h             # PAL interface headers (spiffs, logger, time, wifi, mqtt, …)
    │   ├── esp-idf/            # ESP-IDF implementations
    │   │   └── pal_esp_idf_*.cpp
    │   └── mac-pc/             # macOS / PC implementations (used by ferp-com-simulator)
    │       ├── hsys_config_mac.cpp   # Pure C++17 JSON config engine (no ArduinoJson)
    │       ├── pal_mac_logger.cpp
    │       ├── pal_mac_logger_uart.cpp
    │       ├── pal_mac_spiffs.cpp    # POSIX SPIFFS emulation → <cwd>/SPIFFS/spiffs/
    │       └── pal_mac_time.cpp
    ├── peripheral/             # Generic peripheral drivers (button, LED, buzzer, NTP, …)
    │   ├── hsys_button.h / hsys_buzzer.h / hsys_led.h / hsys_leds.h
    │   ├── hsys_ntp.h / hsys_tog_button.h / hsys_type.h
    │   └── generic_peripheral/
    │       └── hsys_*.cpp
    ├── utils/                  # Utility libraries
    │   ├── base64.hpp/.cpp
    │   ├── crc32.h/.cpp
    │   └── utils.hpp
    └── ferp-device-firmware/   # Legacy device firmware submodule (read-only reference)
        ├── 2303/main_board/    # Board rev 2303 (ESP07 + ESP32)
        ├── 2404/main_board/    # Board rev 2404
        ├── display_simulator/  # PlatformIO display simulators
        ├── ferp_board/         # FERP main board firmware
        ├── esp_data_capture/   # Data capture utility
        ├── pos_printer/        # POS printer
        └── remote_linux/       # Remote flash/watchdog scripts
```

---

## tools/

```
tools/
└── sim-ui/
    ├── sim_ui.py               # Python/Tkinter simulator UI (connects to sim_bridge via TCP)
    └── widgets/
        ├── __init__.py
        ├── config_widget.py
        ├── led_widget.py
        ├── log_widget.py
        ├── mqtt_widget.py
        ├── nozzle_widget.py
        ├── ota_widget.py
        └── pool_widget.py
```

---

## Key Architecture Notes

| Concept | Details |
|---|---|
| **Build targets** | `ferp-com-simulator` (macOS CMake), `ferp-com-esp32-idf` (ESP-IDF) |
| **PAL split** | `pal/esp-idf/` for ESP32, `pal/mac-pc/` for simulator |
| **Config JSON** | File path: `Configs/DeviceConfigs.json` → resolves to `<cwd>/SPIFFS/spiffs/Configs/DeviceConfigs.json` on Mac |
| **Module IDs** | Ticker=1, SysMon=2, SimBridge=3, Spiffs=5, Config=6 |
| **Message IDs** | Defined in `src/product/app/app_msg_ids.h` (canonical) |
| **Lifecycle** | `pre_init` → `init` (subscribe here) → `post_init` (publish here) |
| **TAG size** | PAL enforces exactly 8 characters via `_Static_assert` |
