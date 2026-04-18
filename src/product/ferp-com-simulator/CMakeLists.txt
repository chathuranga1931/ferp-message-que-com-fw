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
set(MOD_CONFIG "${ROOT}/app-modules/module_config")

# Force-include user_config.h as the very first header in every TU so that
# all per-file log enable flags are defined before any other #include.
add_compile_options(-include "${APP}/user_config.h")

set(MIDDLEWARE "${ROOT}/sub-modules/middleware")
set(PERIPHERAL "${ROOT}/sub-modules/peripheral")

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
    ${CMAKE_CURRENT_SOURCE_DIR}/SPIFFS/pal_mac_spiffs.cpp
)
target_include_directories(pal_mac PUBLIC
    ${PAL}        # pal_logger.h, pal_time.h, pal_logger_uart.h
    ${PAL_MAC}    # implementation-private headers (if any)
)
target_link_libraries(pal_mac PRIVATE hsys_os)

# ── 3. middleware (hsys_config — simulator native, no ArduinoJson) ────────────
add_library(middleware STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/hsys_config_mac.cpp
)
target_include_directories(middleware PUBLIC
    ${MIDDLEWARE}   # hsys_config.h
    ${PERIPHERAL}   # hsys_type.h
    ${PAL}          # pal_logger.h
    ${APP}          # user_config.h (for HSYS_CFG_LOG_EN, ERROR_OK, ERR_CONFIG_OFFSET)
)
target_link_libraries(middleware PRIVATE pal_mac)

# ── 4. hsys-framework ─────────────────────────────────────────────────────────
add_library(hsys_framework STATIC
    ${HSYS_FW}/hsys_pool.cpp
    ${HSYS_FW}/hsys_msg.cpp
    ${HSYS_FW}/hsys_module.cpp
    ${HSYS_FW}/hsys_task_mgr.cpp
)
target_include_directories(hsys_framework PUBLIC ${HSYS_FW} ${PAL})
target_link_libraries(hsys_framework PRIVATE hsys_os pal_mac)

# ── 4. app-messages (typed message classes) ───────────────────────────────────
add_library(app_messages STATIC
    ${APP_MSGS}/msg_sensor_data.cpp
    ${APP_MSGS}/msg_tick_1000ms.cpp
    ${APP_MSGS}/msg_spiffs_ready.cpp
    ${APP_MSGS}/msg_config_ready.cpp
    ${APP_MSGS}/msg_config_set.cpp
    ${APP_MSGS}/msg_config_get_request.cpp
)
target_include_directories(app_messages PUBLIC
    ${APP_MSGS}    # msg_*.h, IHsysMsg.h
    ${APP}         # app_msg_ids.h
    ${MOD_CONFIG}  # app_config.h
    ${PERIPHERAL}  # hsys_type.h (pulled in via app_config.h)
    ${PAL}         # pal_logger.h (for FWK_LOG_* via hsys_log.h)
)
target_link_libraries(app_messages PRIVATE hsys_framework)

# ── 5a. app-pheripherals (app_spiffs — mutex-protected PAL wrapper) ───────────
set(APP_PHR "${ROOT}/app-pheripherals")
add_library(app_pheripherals STATIC
    ${APP_PHR}/app_spiffs.cpp
)
target_include_directories(app_pheripherals PUBLIC
    ${APP_PHR}   # app_spiffs.h
    ${PAL}       # pal_spiffs.h, pal_logger.h
    ${HSYS_OS}   # hsys_mutex.h
)
target_link_libraries(app_pheripherals PRIVATE pal_mac hsys_os)

# ── 5. app-modules ────────────────────────────────────────────────────────────
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
    ${APP_MODS}/module_config    # module_config.h
    ${APP_PHR}   # app_spiffs.h
    ${APP}       # app_msg_ids.h, app.h
)
target_link_libraries(app_modules PRIVATE hsys_framework hsys_os app_messages app_pheripherals middleware)

# ── 6. app (product-level) ────────────────────────────────────────────────────
add_library(app_product STATIC
    ${APP}/app.cpp
)
target_include_directories(app_product PUBLIC
    ${APP}
    ${MOD_CONFIG}
    ${MIDDLEWARE}
    ${PERIPHERAL}
    ${APP_MODS}/module_spiffs   # module_spiffs.h
)
target_link_libraries(app_product PRIVATE hsys_framework app_modules app_messages middleware)

# ── 7. sim_bridge (TCP ↔ Python UI) ──────────────────────────────────────────
set(SIM_BRIDGE "${CMAKE_CURRENT_SOURCE_DIR}/sim_bridge")
add_library(sim_bridge STATIC
    ${SIM_BRIDGE}/module_sim_bridge.cpp
)
target_include_directories(sim_bridge PUBLIC
    ${SIM_BRIDGE}   # module_sim_bridge.h
    ${HSYS_FW}      # hsys_module.h, hsys_msg.h
    ${HSYS_OS}      # hsys_mutex.h, hsys_types.h
    ${APP}          # app_msg_ids.h
    ${APP_MSGS}     # msg_tick_1000ms.h etc.
    ${PAL}          # pal_time.h
)
target_link_libraries(sim_bridge PRIVATE hsys_framework hsys_os pal_mac app_messages)

# ── 8. main executable ────────────────────────────────────────────────────────
# into the executable (not a static library) so the linker picks it up.
add_executable(ferp-com-simulator
    main/main.cpp
    sim_init.cpp                 # simulator-extended app_init (adds sim_bridge task)
)
target_include_directories(ferp-com-simulator PRIVATE
    ${HSYS_FW}                   # hsys_log.h, hsys_module.h
    ${HSYS_OS}                   # hsys_mutex.h, hsys_types.h
    ${PAL}                       # pal_logger.h
    ${SIM_BRIDGE}                # module_sim_bridge.h
    ${CMAKE_CURRENT_SOURCE_DIR}  # sim_init.h
    ${APP}                       # app.h, app_msg_ids.h
    ${APP_MSGS}                  # msg_*.h
    ${APP_MODS}                  # app_msg_table.h
    ${APP_MODS}/ticker
    ${APP_MODS}/module_sysmon
    ${APP_MODS}/module_spiffs    # module_spiffs.h
    ${APP_MODS}/module_config    # module_config.h
    ${APP_PHR}                   # app_spiffs.h
)
target_link_libraries(ferp-com-simulator PRIVATE
    app_product app_messages hsys_framework hsys_os pal_mac sim_bridge app_modules app_pheripherals
)
