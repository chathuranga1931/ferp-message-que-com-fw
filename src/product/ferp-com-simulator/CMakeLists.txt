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
set(MIDDLEWARE "${ROOT}/sub-modules/middleware")
set(PERIPHERAL "${ROOT}/sub-modules/peripheral")
set(APP_MSGS   "${ROOT}/app-messages")
set(APP_MODS   "${ROOT}/app-modules")
set(APP_PHR    "${ROOT}/app-pheripherals")
set(APP        "${ROOT}/product/app")
set(SIM_BRIDGE "${CMAKE_CURRENT_SOURCE_DIR}/sim_bridge")

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

# ── 2. PAL — mac-pc implementations ──────────────────────────────────────────
# All platform-specific implementations live here.
# pal_spiffs.h interface is in PAL/; the POSIX impl is in mac-pc/.
add_library(pal_mac STATIC
    ${PAL_MAC}/pal_mac_time.cpp
    ${PAL_MAC}/pal_mac_logger_uart.cpp
    ${PAL_MAC}/pal_mac_logger.cpp
    ${PAL_MAC}/pal_mac_spiffs.cpp       # POSIX emulation of SPIFFS
    ${PAL_MAC}/hsys_config_mac.cpp      # plain-C++ hsys_config (no ArduinoJson)
)
target_include_directories(pal_mac PUBLIC
    ${PAL}          # all pal_*.h interfaces
    ${MIDDLEWARE}   # hsys_config.h  (needed by hsys_config_mac.cpp)
    ${PERIPHERAL}   # hsys_type.h
    ${APP}          # user_config.h
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
)
target_include_directories(app_messages PUBLIC
    ${APP_MSGS}
    ${APP}           # app_msg_ids.h
    ${APP_MODS}/module_config   # app_config.h
    ${PERIPHERAL}    # hsys_type.h
    ${PAL}           # pal_logger.h
)
target_link_libraries(app_messages PRIVATE hsys_framework)

# ── 5. app-pheripherals ───────────────────────────────────────────────────────
add_library(app_pheripherals STATIC
    ${APP_PHR}/app_spiffs.cpp
)
target_include_directories(app_pheripherals PUBLIC
    ${APP_PHR}   # app_spiffs.h
    ${PAL}       # pal_spiffs.h, pal_logger.h
    ${HSYS_OS}   # hsys_mutex.h
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
)
target_include_directories(app_modules PUBLIC
    ${APP_MODS}
    ${APP_MODS}/ticker
    ${APP_MODS}/module_a
    ${APP_MODS}/module_b
    ${APP_MODS}/module_sysmon
    ${APP_MODS}/module_spiffs
    ${APP_MODS}/module_config
    ${APP_PHR}       # app_spiffs.h
    ${APP}           # app_msg_ids.h, app.h
    ${MIDDLEWARE}    # hsys_config.h
    ${PERIPHERAL}    # hsys_type.h
)
target_link_libraries(app_modules PRIVATE
    hsys_framework hsys_os app_messages app_pheripherals pal_mac)

# ── 7. app (product-level glue: app.cpp owns _app_config + config handle) ─────
add_library(app_product STATIC
    ${APP}/app.cpp
)
target_include_directories(app_product PUBLIC
    ${APP}
    ${APP_MODS}/module_config
    ${MIDDLEWARE}
    ${PERIPHERAL}
    ${APP_MODS}/module_spiffs
)
target_link_libraries(app_product PRIVATE hsys_framework app_modules app_messages pal_mac)

# ── 8. sim_bridge (TCP <-> Python UI) — simulator-only ───────────────────────
add_library(sim_bridge STATIC
    ${SIM_BRIDGE}/module_sim_bridge.cpp
)
target_include_directories(sim_bridge PUBLIC
    ${SIM_BRIDGE}
    ${HSYS_FW}
    ${HSYS_OS}
    ${APP}
    ${APP_MSGS}
    ${PAL}
)
target_link_libraries(sim_bridge PRIVATE hsys_framework hsys_os pal_mac app_messages)

# ── 8. Executable ─────────────────────────────────────────────────────────────
add_executable(ferp-com-simulator
    main/main.cpp
)
target_include_directories(ferp-com-simulator PRIVATE
    ${HSYS_FW}
    ${HSYS_OS}
    ${PAL}
    ${SIM_BRIDGE}
    ${APP}
    ${APP_MSGS}
    ${APP_MODS}
    ${APP_MODS}/ticker
    ${APP_MODS}/module_sysmon
    ${APP_MODS}/module_spiffs
    ${APP_MODS}/module_config
    ${APP_PHR}
    ${MIDDLEWARE}
    ${PERIPHERAL}
)
target_link_libraries(ferp-com-simulator PRIVATE
    hsys_framework hsys_os pal_mac
    app_messages app_pheripherals app_modules app_product
    sim_bridge
)

# Tell the binary where its own source tree lives so it can chdir to it
# at startup — this ensures SPIFFS/spiffs/ is always found relative to
# the ferp-com-simulator folder, regardless of where the binary is invoked.
target_compile_definitions(ferp-com-simulator PRIVATE
    SIMULATOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
