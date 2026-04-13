cmake_minimum_required(VERSION 3.16)
project(ferp-com-simulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Paths (relative to this CMakeLists.txt) ───────────────────────────────────
set(ROOT       "${CMAKE_CURRENT_SOURCE_DIR}/../..")   # src/
set(HSYS_OS    "${ROOT}/sub-modules/hsys-os")
set(HSYS_FW    "${ROOT}/sub-modules/hsys-framework")
set(PAL        "${ROOT}/sub-modules/pal")
set(PAL_MAC    "${PAL}/mac-pc")
set(APP_MSGS   "${ROOT}/app-messages")
set(APP_MODS   "${ROOT}/app-modules")
set(APP        "${ROOT}/product/app")

# ── 1. hsys-os (macOS) ────────────────────────────────────────────────────────
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

# ── 2. PAL — mac-pc implementations ──────────────────────────────────────────
add_library(pal_mac STATIC
    ${PAL_MAC}/pal_mac_time.cpp
    ${PAL_MAC}/pal_mac_logger_uart.cpp
    ${PAL_MAC}/pal_mac_logger.cpp
)
target_include_directories(pal_mac PUBLIC
    ${PAL}        # pal_logger.h, pal_time.h, pal_logger_uart.h
    ${PAL_MAC}    # implementation-private headers (if any)
)
target_link_libraries(pal_mac PRIVATE hsys_os)

# ── 3. hsys-framework ─────────────────────────────────────────────────────────
add_library(hsys_framework STATIC
    ${HSYS_FW}/hsys_pool.cpp
    ${HSYS_FW}/hsys_msg.cpp
    ${HSYS_FW}/hsys_module.cpp
    ${HSYS_FW}/hsys_task_mgr.cpp
    ${HSYS_FW}/hsys_framework_hooks.cpp   # weak defaults — overridden by product layer
)
target_include_directories(hsys_framework PUBLIC ${HSYS_FW})
target_link_libraries(hsys_framework PRIVATE hsys_os)

# ── 4. app-messages (typed message classes) ───────────────────────────────────
add_library(app_messages STATIC
    ${APP_MSGS}/msg_sensor_data.cpp
    ${APP_MSGS}/msg_tick_1000ms.cpp
)
target_include_directories(app_messages PUBLIC
    ${APP_MSGS}    # msg_*.h, IHsysMsg.h
    ${APP}         # app_msg_ids.h
)
target_link_libraries(app_messages PRIVATE hsys_framework)

# ── 5. app-modules ────────────────────────────────────────────────────────────
add_library(app_modules STATIC
    ${APP_MODS}/ticker/ticker.cpp
    ${APP_MODS}/module_a/module_a.cpp
    ${APP_MODS}/module_b/module_b.cpp
    ${APP_MODS}/module_sysmon/module_sysmon.cpp
)
target_include_directories(app_modules PUBLIC
    ${APP_MODS}
    ${APP_MODS}/ticker
    ${APP_MODS}/module_a
    ${APP_MODS}/module_b
    ${APP_MODS}/module_sysmon
)
target_link_libraries(app_modules PRIVATE hsys_framework hsys_os app_messages)

# ── 6. app (product-level) ────────────────────────────────────────────────────
add_library(app_product STATIC
    ${APP}/app.cpp
)
target_include_directories(app_product PUBLIC ${APP})
target_link_libraries(app_product PRIVATE hsys_framework app_modules app_messages)

# ── 7. main executable ────────────────────────────────────────────────────────
# hsys_framework_hooks.cpp here is the *strong* override — it replaces the
# weak defaults compiled into hsys_framework.  It must be linked directly
# into the executable (not a static library) so the linker picks it up.
add_executable(ferp-com-simulator
    main/main.cpp
    hsys_framework_hooks.cpp   # strong overrides: routes hsys_log → pal_logger
)
target_include_directories(ferp-com-simulator PRIVATE
    ${HSYS_FW}   # hsys_log.h
    ${PAL}       # pal_logger.h
)
target_link_libraries(ferp-com-simulator PRIVATE
    app_product app_messages hsys_framework hsys_os pal_mac
)
