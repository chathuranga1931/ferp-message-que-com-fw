cmake_minimum_required(VERSION 3.16)
project(ferp-com-simulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Paths ─────────────────────────────────────────────────────────────────────
set(ROOT       "${CMAKE_CURRENT_SOURCE_DIR}/../..")   # src/

set(APP_MSGS   "${ROOT}/app-messages")
set(APP_MODS   "${ROOT}/app-modules")
set(APP_PHR    "${ROOT}/app-pheripherals")
set(APP        "${ROOT}/product/app")

set(ARDUINOJSON "${ROOT}/managed_components/bblanchon__arduinojson")

set(SUB_MODS   "${ROOT}/sub-modules")

set(HSYS_FW    "${SUB_MODS}/hsys-framework")
set(HSYS_OS    "${SUB_MODS}/hsys-os")
set(PAL        "${SUB_MODS}/pal")
set(MIDDLEWARE "${SUB_MODS}/middleware")
set(PERIPHERAL "${SUB_MODS}/peripheral")

# DT board device library (display_types.h, com_distap.h, cmd_distap.h)
set(DISTAP_LIB "${SUB_MODS}/ferp-device-firmware/ferp_board/main-esp32/lib/device")
set(DRIVERS    "${SUB_MODS}/drivers")

set(PAL_MAC    "${PAL}/mac-pc")
set(PAL_MAC_DRV "${PAL_MAC}/driver")

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
    ${PAL_MAC}/pal_mac_sd.cpp               # POSIX SD card emulation (SDCARD/ folder)
    ${PAL_MAC}/pal_mac_gpio.cpp             # GPIO → mac_driver_send_gpio()
    ${PAL_MAC}/pal_mac_system.cpp           # pal_system_init() → boot everything
    ${PAL_MAC}/pal_mac_network.cpp          # pal_network_ping() via TCP connect
    ${PAL_MAC}/pal_mac_wifi.cpp             # pal_wifi_*() simulator stub
    ${PAL_MAC}/pal_mac_i2c.cpp              # pal_i2c_*() emulator routing
    ${PAL_MAC}/pal_mac_ntp.cpp              # pal_ntp_*() host-clock stub
    ${PAL_MAC}/ds1307_i2c_emulator.cpp      # DS1307 register-bank emulator
    # Common middleware — uses ArduinoJson (platform-independent)
    ${MIDDLEWARE}/hsys_config.cpp
    # Simulator driver — owns the TCP socket and all UI I/O
    ${PAL_MAC_DRV}/mac_driver.cpp
    # Sim bridge — HSYS module that serialises app messages to the UI
    ${PAL_MAC_DRV}/module_sim_bridge.cpp
    # Sim message injector — receives SIM_MSG_INJECT and injects into HSYS bus
    ${PAL_MAC_DRV}/sim_msg_inject.cpp
    # Simulator implementations of DT board comms APIs
    ${PAL_MAC_DRV}/mac_com_distap.cpp
    ${PAL_MAC_DRV}/mac_cmd_distap.cpp
    # HTTP client (libcurl), crypto (CommonCrypto), and eFuse MAC — needed by cube_sphere_api
    ${PAL_MAC}/pal_mac_http_client.cpp
    ${PAL_MAC}/pal_mac_crypto.cpp
    ${PAL_MAC}/pal_mac_efuse.cpp
    # CubeSphere cloud driver
    ${DRIVERS}/cube_sphere/cube_sphere_api.cpp
)
target_include_directories(pal_mac PUBLIC
    ${PAL}          # pal_*.h interfaces + pal_system.h
    ${PAL_MAC}      # pal_mac_*.h + esp_err.h stub
    ${PAL_MAC_DRV}  # mac_driver.h, module_sim_bridge.h
    ${MIDDLEWARE}   # hsys_config.h — must come before HSYS_FW to avoid shim collision
    ${HSYS_FW}      # hsys_module.h, hsys_pool.h, hsys_msg.h
    ${HSYS_OS}      # hsys_task_desc_t
    ${PERIPHERAL}   # hsys_type.h
    ${APP}          # app.h, app_msg_ids.h, user_config.h
    ${APP_MSGS}     # msg_tick_1000ms.h etc.
    ${APP_MODS}/module_config   # app_config.h (needed by app.h transitively)
    ${APP_MODS}/module_fuel                        # module_fuel.h
    ${APP_MODS}/module_fuel/pumps                  # nozzle_event.h
    ${DISTAP_LIB}   # display_types.h, com_distap.h, cmd_distap.h
    ${ARDUINOJSON}      # ArduinoJson.h (root)
    ${ARDUINOJSON}/src  # ArduinoJson internal headers (angle-bracket includes)
    ${DRIVERS}/cube_sphere  # cube_sphere_api.h, cube_sphere_config.h
)
target_link_libraries(pal_mac PRIVATE hsys_os curl)
target_compile_definitions(pal_mac PRIVATE FERP_SIMULATOR=1)

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
    ${APP_MSGS}/msg_sd_ready.cpp
    ${APP_MSGS}/msg_sd_status.cpp
    ${APP_MSGS}/msg_config_ready.cpp
    ${APP_MSGS}/msg_config_set.cpp
    ${APP_MSGS}/msg_config_get.cpp
    ${APP_MSGS}/msg_config_get_wifi.cpp
    ${APP_MSGS}/msg_config_get_cloud.cpp
    ${APP_MSGS}/msg_config_get_mqtt.cpp
    ${APP_MSGS}/msg_config_get_dt.cpp
    ${APP_MSGS}/msg_config_wifi.cpp
    ${APP_MSGS}/msg_config_cloud.cpp
    ${APP_MSGS}/msg_config_mqtt.cpp
    ${APP_MSGS}/msg_config_dt.cpp
    ${APP_MSGS}/msg_timer_start.cpp
    ${APP_MSGS}/msg_timer_stop.cpp
    ${APP_MSGS}/msg_timer_start_response.cpp
    ${APP_MSGS}/msg_timer_stop_response.cpp
    ${APP_MSGS}/msg_timer_alarm.cpp
    ${APP_MSGS}/msg_default_btn.cpp
    ${APP_MSGS}/msg_printer_btn.cpp
    ${APP_MSGS}/msg_fuel_pumped.cpp
    ${APP_MSGS}/msg_nozzle_state.cpp
    ${APP_MSGS}/msg_wifi_event.cpp
    ${APP_MSGS}/msg_internet_status.cpp
    ${APP_MSGS}/msg_cloud_status.cpp
    ${APP_MSGS}/msg_time_status.cpp
)
target_include_directories(app_messages PUBLIC
    ${APP_MSGS}
    ${APP}           # app_msg_ids.h
    ${APP_MODS}/module_config
    ${APP_MODS}/module_fuel/pumps   # nozzle_event.h (needed by msg_nozzle_state.h)
    ${PERIPHERAL}
    ${PAL}
    ${PAL_MAC}       # esp_err.h stub
    ${DISTAP_LIB}    # display_types.h (needed transitively)
    ${ARDUINOJSON}       # ArduinoJson.h — needed by from_json() implementations
    ${ARDUINOJSON}/src   # ArduinoJson internal headers
)
target_compile_definitions(app_messages PRIVATE FERP_SIMULATOR=1)
target_link_libraries(app_messages PRIVATE hsys_framework)

# ── 5. app-pheripherals ───────────────────────────────────────────────────────
add_library(app_pheripherals STATIC
    ${APP_PHR}/app_spiffs.cpp
    ${APP_PHR}/app_sd.cpp
    ${PERIPHERAL}/generic_peripheral/hsys_led.cpp
    ${PERIPHERAL}/generic_peripheral/hsys_buzzer.cpp
    ${PERIPHERAL}/generic_peripheral/hsys_button.cpp
    ${PERIPHERAL}/generic_peripheral/hsys_tog_button.cpp
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
    # Fuel module
    ${APP_MODS}/module_fuel/module_fuel.cpp
    ${APP_MODS}/module_fuel/fuel_disptap_driver.cpp
    ${APP_MODS}/module_fuel/pumps/sanki/sanki_6_digit_1.cpp
    # Buzzer module
    ${APP_MODS}/module_buzzer/module_buzzer.cpp
    # Cloud module (uses cloud_driver_t abstraction)
    ${APP_MODS}/module_cloud/module_cloud.cpp
    ${DRIVERS}/cube_sphere/cube_sphere_cloud_driver.cpp
    # Internet monitor module
    ${APP_MODS}/module_internet/module_internet.cpp
    # WiFi module
    ${APP_MODS}/module_wifi/module_wifi.cpp
    # SD card module
    ${APP_MODS}/module_sd/module_sd.cpp
    # Time manager module
    ${APP_MODS}/module_timemgr/module_timemgr.cpp
    # DS1307 hardware driver
    ${DRIVERS}/DS1307/ds1307.cpp
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
    ${APP_MODS}/module_fuel
    ${APP_MODS}/module_fuel/pumps
    ${APP_MODS}/module_fuel/pumps/sanki
    ${APP_MODS}/module_buzzer
    ${APP_MODS}/module_cloud
    ${APP_MODS}/module_internet
    ${APP_MODS}/module_wifi
    ${APP_MODS}/module_sd
    ${APP_MODS}/module_timemgr
    ${DRIVERS}/DS1307
    ${DRIVERS}/cube_sphere  # cube_sphere_api.h, cube_sphere_config.h
    ${SUB_MODS}           # for ds1307.cpp: #include "pal/pal_i2c.h"
    ${APP_PHR}
    ${APP}
    ${PAL_MAC}       # esp_err.h stub
    ${DISTAP_LIB}    # display_types.h, com_distap.h, cmd_distap.h
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
    ${APP_MODS}/module_fuel
    ${APP_MODS}/module_buzzer
    ${APP_MODS}/module_cloud
    ${APP_MODS}/module_internet
    ${APP_MODS}/module_wifi
    ${APP_MODS}/module_sd
    ${APP_MODS}/module_timemgr
    ${APP_MSGS}
    ${HSYS_OS}
    ${PAL_MAC}       # esp_err.h stub
    ${DISTAP_LIB}    # display_types.h (transitively needed by module_fuel.h)
    ${MIDDLEWARE}
    ${PERIPHERAL}
    ${PAL}           # pal_system.h
    ${DRIVERS}/cube_sphere  # cube_sphere_cloud_driver.h
)
target_link_libraries(app_product PRIVATE hsys_framework app_modules app_messages pal_mac)

# ── 8. Executable — just main.cpp ────────────────────────────────────────────
add_executable(ferp-com-simulator
    main/main.cpp
)
target_include_directories(ferp-com-simulator PRIVATE
    ${MIDDLEWARE}   # hsys_config.h — before PAL to avoid shim collision
    ${PAL}          # pal_logger.h
    ${PAL_MAC}      # ds1307_i2c_emulator.h, pal_mac_i2c_emulator.h
    ${APP}          # app.h, app_hw_config.h
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

