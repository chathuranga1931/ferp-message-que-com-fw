cmake_minimum_required(VERSION 3.16)
project(ferp-com-simulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Paths (relative to this CMakeLists.txt) ───────────────────────────────────
set(ROOT       "${CMAKE_CURRENT_SOURCE_DIR}/../..")   # src/
set(HSYS_OS    "${ROOT}/sub-modules/hsys-os")
set(HSYS_FW    "${ROOT}/sub-modules/hsys-framework")
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

# ── 2. hsys-framework ─────────────────────────────────────────────────────────
add_library(hsys_framework STATIC
    ${HSYS_FW}/hsys_pool.cpp
    ${HSYS_FW}/hsys_msg.cpp
    ${HSYS_FW}/hsys_module.cpp
    ${HSYS_FW}/hsys_task_mgr.cpp
)
target_include_directories(hsys_framework PUBLIC ${HSYS_FW})
target_link_libraries(hsys_framework PRIVATE hsys_os)

# ── 3. app-modules ────────────────────────────────────────────────────────────
add_library(app_modules STATIC
    ${APP_MODS}/ticker/ticker.cpp
    ${APP_MODS}/module_a/module_a.cpp
    ${APP_MODS}/module_b/module_b.cpp
)
target_include_directories(app_modules PUBLIC
    ${APP_MODS}
    ${APP_MODS}/ticker
    ${APP_MODS}/module_a
    ${APP_MODS}/module_b
)
target_link_libraries(app_modules PRIVATE hsys_framework hsys_os)

# ── 4. app (product-level) ────────────────────────────────────────────────────
add_library(app_product STATIC
    ${APP}/app.cpp
)
target_include_directories(app_product PUBLIC ${APP})
target_link_libraries(app_product PRIVATE hsys_framework app_modules)

# ── 5. main executable ────────────────────────────────────────────────────────
add_executable(ferp-com-simulator
    main/main.cpp
)
target_link_libraries(ferp-com-simulator PRIVATE app_product hsys_framework hsys_os)
