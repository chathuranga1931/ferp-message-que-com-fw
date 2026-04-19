cmake_minimum_required(VERSION 3.16)
project(ferp-com-simulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Paths ─────────────────────────────────────────────────────────────────────
set(ROOT       "${CMAKE_CURRENT_SOURCE_DIR}/../..")   # src/
set(HSYS_OS    "${ROOT}/sub-modules/hsys-os")
set(HSYS_FW    "${ROOT}/sub-modules/hsys-framework")
set(PAL        "${ROOT}/sub-modules/pal")
set(PAL_MAC    "${PAL}/mac-pc")
set(PAL_MAC_DRV "${PAL_MAC}/driver")
set(MIDDLEWARE "${ROOT}/sub-modules/middleware")
set(PERIPHERAL "${ROOT}/sub-modules/peripheral")
set(APP_MSGS   "${ROOT}/app-messages")
set(APP_MODS   "${ROOT}/app-modules")
set(APP_PHR    "${ROOT}/app-pheripherals")
set(APP        "${ROOT}/product/app")
set(ARDUINOJSON "${ROOT}/managed_components/bblanchon__arduinojson")

# Force-include user_config.h first in every TU
add_compile_options(-include "${APP}/user_config.h")

# ── 1. hsys-os (macOS pthreads) ───────────────────────────────────────────────
add_library(hsys_os STATIC
    ${HSYS_OS}/mac_os/hsys_task.cpp
    ${HSYS_OS}/mac_os/hsys_task_append.cpp
    ${HSYS_OS}/mac_os/hsys_mutex.cpp
    ${HSYS_OS}/mac_os/hsys_queue.cpp
    ${HSYS_OS}/mac_os/hsys_semaphore.cpp
    ${HSYS_OS}/mac_os/hsys_event.cpp
    ${HSYS_OS}/mac_os/hsys_soft_timer.cpp
)
target_include_directories(hsys_os PUBLIC ${HSYS_OS})

# ── 2. PAL mac-pc ─────────────────────────────────────────────────────────────
# Everything simulator-specific lives here — PAL implementations, the TCP
# driver, and the sim bridge module.  The ferp-com-simulator executable only
# needs main.cpp on top of this.
add_library(pal_mac STATIC
    # Platform PAL implementations
    ${PAL_MAC}/pal_mac_time.cpp
    ${PAL_MAC}/pal_mac_logger.cpp
    ${PAL_MAC}/pal_mac_logger_uart.cpp
    ${PAL_MAC}/pal_mac_spiffs.cpp           # POSIX SPIFFS emulation
    ${PAL_MAC}/pal_mac_gpio.cpp             # GPIO → mac_driver_send_gpio()
    ${PAL_MAC}/pal_mac_system.cpp           # pal_system_init() → boot everything
    # Common middleware — uses ArduinoJson (platform-independent)
    ${MIDDLEWARE}/hsys_config.cpp
    # Simulator driver — owns the TCP socket and all UI I/O
    ${PAL_MAC_DRV}/mac_driver.cpp
    # Sim bridge — HSYS module that serialises app messages to the UI
    ${PAL_MAC_DRV}/module_sim_bridge.cpp
)
target_include_directories(pal_mac PUBLIC
    ${PAL}          # pal_*.h interfaces + pal_system.h
    ${PAL_MAC}      # pal_mac_*.h
    ${PAL_MAC_DRV}  # mac_driver.h, module_sim_bridge.h
    ${MIDDLEWARE}   # hsys_config.h — must come before HSYS_FW to avoid shim collision
    ${HSYS_FW}      # hsys_module.h, hsys_pool.h, hsys_msg.h
    ${HSYS_OS}      # hsys_task_desc_t
    ${PERIPHERAL}   # hsys_type.h
    ${APP}          # app.h, app_msg_ids.h, user_config.h
    ${APP_MSGS}     # msg_tick_1000ms.h etc.
    ${APP_MODS}/module_config   # app_config.h (needed by app.h transitively)
    ${ARDUINOJSON}      # ArduinoJson.h (root)
    ${ARDUINOJSON}/src  # ArduinoJson internal headers (angle-bracket includes)
)
target_link_libraries(pal_mac PRIVATE hsys_os)

# ── 3. hsys-framework ─────────────────────────────────────────────────────────
add_library(hsys_framework STATIC
    ${HSYS_FW}/hsys_pool.cpp
    ${HSYS_FW}/hsys_msg.cpp
    ${HSYS_FW}/hsys_module.cpp
    ${HSYS_FW}/hsys_task_mgr.cpp
)
target_include_directories(hsys_framework PUBLIC ${HSYS_FW} ${PAL})
target_link_libraries(hsys_framework PRIVATE hsys_os pal_mac)

# ── 4. app-messages ───────────────────────────────────────────────────────────
add_library(app_messages STATIC
    ${APP_MSGS}/msg_sensor_data.cpp
    ${APP_MSGS}/msg_tick_1000ms.cpp
    ${APP_MSGS}/msg_spiffs_ready.cpp
    ${APP_MSGS}/msg_config_ready.cpp
    ${APP_MSGS}/msg_config_set.cpp
    ${APP_MSGS}/msg_config_get.cpp
    ${APP_MSGS}/msg_timer_start.cpp
    ${APP_MSGS}/msg_timer_stop.cpp
    ${APP_MSGS}/msg_timer_start_response.cpp
    ${APP_MSGS}/msg_timer_stop_response.cpp
    ${APP_MSGS}/msg_timer_alarm.cpp
    ${APP_MSGS}/msg_default_btn.cpp
    ${APP_MSGS}/msg_printer_btn.cpp
)
target_include_directories(app_messages PUBLIC
    ${APP_MSGS}
    ${APP}           # app_msg_ids.h
    ${APP_MODS}/module_config
    ${PERIPHERAL}
    ${PAL}
)
target_link_libraries(app_messages PRIVATE hsys_framework)

# ── 5. app-pheripherals ───────────────────────────────────────────────────────
add_library(app_pheripherals STATIC
    ${APP_PHR}/app_spiffs.cpp
    ${PERIPHERAL}/generic_peripheral/hsys_led.cpp
    ${PERIPHERAL}/generic_peripheral/hsys_button.cpp
)
target_include_directories(app_pheripherals PUBLIC
    ${APP_PHR}
    ${PERIPHERAL}
    ${PAL}
    ${HSYS_OS}
)
target_link_libraries(app_pheripherals PRIVATE pal_mac hsys_os)

# ── 6. app-modules ────────────────────────────────────────────────────────────
add_library(app_modules STATIC
    ${APP_MODS}/ticker/ticker.cpp
    ${APP_MODS}/module_a/module_a.cpp
    ${APP_MODS}/module_b/module_b.cpp
    ${APP_MODS}/module_sysmon/module_sysmon.cpp
    ${APP_MODS}/module_spiffs/module_spiffs.cpp
    ${APP_MODS}/module_config/module_config.cpp
    ${APP_MODS}/module_timer/module_timer.cpp
    ${APP_MODS}/module_leds/module_leds.cpp
    ${APP_MODS}/module_default_btn/module_default_btn.cpp
    ${APP_MODS}/module_print_btn/module_print_btn.cpp
)
target_include_directories(app_modules PUBLIC
    ${APP_MODS}
    ${APP_MODS}/ticker
    ${APP_MODS}/module_a
    ${APP_MODS}/module_b
    ${APP_MODS}/module_sysmon
    ${APP_MODS}/module_spiffs
    ${APP_MODS}/module_config
    ${APP_MODS}/module_timer
    ${APP_MODS}/module_leds
    ${APP_MODS}/module_default_btn
    ${APP_MODS}/module_print_btn
    ${APP_PHR}
    ${APP}
    ${MIDDLEWARE}
    ${PERIPHERAL}
)
target_link_libraries(app_modules PRIVATE
    hsys_framework hsys_os app_messages app_pheripherals pal_mac)

# ── 7. app product (shared app.cpp) ───────────────────────────────────────────
add_library(app_product STATIC
    ${APP}/app.cpp
)
target_include_directories(app_product PUBLIC
    ${APP}
    ${APP_MODS}
    ${APP_MODS}/ticker
    ${APP_MODS}/module_a
    ${APP_MODS}/module_b
    ${APP_MODS}/module_sysmon
    ${APP_MODS}/module_spiffs
    ${APP_MODS}/module_config
    ${APP_MODS}/module_timer
    ${APP_MODS}/module_leds
    ${APP_MODS}/module_default_btn
    ${APP_MODS}/module_print_btn
    ${APP_MSGS}
    ${HSYS_OS}
    ${MIDDLEWARE}
    ${PERIPHERAL}
    ${PAL}           # pal_system.h
)
target_link_libraries(app_product PRIVATE hsys_framework app_modules app_messages pal_mac)

# ── 8. Executable — just main.cpp ────────────────────────────────────────────
add_executable(ferp-com-simulator
    main/main.cpp
)
target_include_directories(ferp-com-simulator PRIVATE
    ${MIDDLEWARE}   # hsys_config.h — before PAL to avoid shim collision
    ${PAL}          # pal_logger.h
    ${APP}          # app.h
)
target_link_libraries(ferp-com-simulator PRIVATE
    hsys_framework hsys_os pal_mac
    app_messages app_pheripherals app_modules app_product
)

# Tell the binary where its own source tree lives so it can chdir to it
# at startup — this ensures SPIFFS/spiffs/ is always found relative to
# the ferp-com-simulator folder, regardless of where the binary is invoked.
target_compile_definitions(ferp-com-simulator PRIVATE
    SIMULATOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

