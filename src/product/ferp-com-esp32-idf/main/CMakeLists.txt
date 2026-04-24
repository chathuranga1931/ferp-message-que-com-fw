# =============================================================================
# All application sources for the ESP-IDF build.
# Path roots relative to this file (src/product/ferp-com-esp32-idf/main/).
# =============================================================================
set(D_APP  "${CMAKE_CURRENT_SOURCE_DIR}/../../app")
set(D_MODS "${CMAKE_CURRENT_SOURCE_DIR}/../../../app-modules")
set(D_MSGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../app-messages")
set(D_PHER "${CMAKE_CURRENT_SOURCE_DIR}/../../../app-pheripherals")
set(D_SUB  "${CMAKE_CURRENT_SOURCE_DIR}/../../../sub-modules")

idf_component_register(
    SRCS
        # ── Entry point ───────────────────────────────────────────────────
        "main.cpp"

        # ── Application ───────────────────────────────────────────────────
        "${D_APP}/app.cpp"

        # ── App modules ───────────────────────────────────────────────────
        "${D_MODS}/ticker/ticker.cpp"
        "${D_MODS}/module_a/module_a.cpp"
        "${D_MODS}/module_b/module_b.cpp"
        "${D_MODS}/module_sysmon/module_sysmon.cpp"
        "${D_MODS}/module_spiffs/module_spiffs.cpp"
        "${D_MODS}/module_config/module_config.cpp"
        "${D_MODS}/module_timer/module_timer.cpp"
        "${D_MODS}/module_leds/module_leds.cpp"
        "${D_MODS}/module_default_btn/module_default_btn.cpp"
        "${D_MODS}/module_print_btn/module_print_btn.cpp"
        "${D_MODS}/module_fuel/module_fuel.cpp"
        "${D_MODS}/module_fuel/fuel_disptap_driver.cpp"
        "${D_MODS}/module_fuel/pumps/sanki/sanki_6_digit_1.cpp"
        "${D_MODS}/module_buzzer/module_buzzer.cpp"
        "${D_MODS}/module_cloud/module_cloud.cpp"
        "${D_MODS}/module_internet/module_internet.cpp"
        "${D_MODS}/module_wifi/module_wifi.cpp"
        "${D_MODS}/module_sd/module_sd.cpp"
        "${D_MODS}/module_timemgr/module_timemgr.cpp"

        # ── App messages ──────────────────────────────────────────────────
        "${D_MSGS}/msg_sensor_data.cpp"
        "${D_MSGS}/msg_tick_1000ms.cpp"
        "${D_MSGS}/msg_spiffs_ready.cpp"
        "${D_MSGS}/msg_sd_ready.cpp"
        "${D_MSGS}/msg_sd_status.cpp"
        "${D_MSGS}/msg_config_ready.cpp"
        "${D_MSGS}/msg_config_set.cpp"
        "${D_MSGS}/msg_config_get.cpp"
        "${D_MSGS}/msg_config_get_wifi.cpp"
        "${D_MSGS}/msg_config_get_cloud.cpp"
        "${D_MSGS}/msg_config_get_mqtt.cpp"
        "${D_MSGS}/msg_config_get_dt.cpp"
        "${D_MSGS}/msg_config_wifi.cpp"
        "${D_MSGS}/msg_config_cloud.cpp"
        "${D_MSGS}/msg_config_mqtt.cpp"
        "${D_MSGS}/msg_config_dt.cpp"
        "${D_MSGS}/msg_timer_start.cpp"
        "${D_MSGS}/msg_timer_stop.cpp"
        "${D_MSGS}/msg_timer_start_response.cpp"
        "${D_MSGS}/msg_timer_stop_response.cpp"
        "${D_MSGS}/msg_timer_alarm.cpp"
        "${D_MSGS}/msg_default_btn.cpp"
        "${D_MSGS}/msg_printer_btn.cpp"
        "${D_MSGS}/msg_fuel_pumped.cpp"
        "${D_MSGS}/msg_nozzle_state.cpp"
        "${D_MSGS}/msg_wifi_event.cpp"
        "${D_MSGS}/msg_internet_status.cpp"
        "${D_MSGS}/msg_cloud_status.cpp"
        "${D_MSGS}/msg_time_status.cpp"

        # ── App peripherals ───────────────────────────────────────────────
        "${D_PHER}/app_spiffs.cpp"
        "${D_PHER}/app_sd.cpp"

        # ── Generic peripheral implementations ────────────────────────────
        "${D_SUB}/peripheral/generic_peripheral/hsys_led.cpp"
        "${D_SUB}/peripheral/generic_peripheral/hsys_buzzer.cpp"
        "${D_SUB}/peripheral/generic_peripheral/hsys_button.cpp"
        "${D_SUB}/peripheral/generic_peripheral/hsys_tog_button.cpp"

        # ── Middleware ────────────────────────────────────────────────────
        "${D_SUB}/middleware/hsys_config.cpp"
        "${D_SUB}/middleware/list-manager/list_manager.cpp"

        # ── PAL ESP-IDF implementations ───────────────────────────────────
        "${D_SUB}/pal/esp-idf/pal_esp_idf_time.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_logger.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_logger_uart.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_spiffs.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_sd.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_gpio.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_system.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_network.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_wifi.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_i2c.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_ntp.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_power.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_mqtt.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_http_client.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_uart.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_efuse.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_crypto.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_fw_update.cpp"

        # ── DISTAP device library ─────────────────────────────────────────
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/com_distap.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/cmd_distap.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/board.c"

        # ── External drivers ──────────────────────────────────────────────
        "${D_SUB}/drivers/DS1307/ds1307.cpp"
        "${D_SUB}/drivers/cube_sphere/cube_sphere_api.cpp"
        "${D_SUB}/drivers/cube_sphere/cube_sphere_cloud_driver.cpp"
    INCLUDE_DIRS
        # Entry point + app
        "."
        "${D_APP}"
        # App modules
        "${D_MODS}"
        "${D_MODS}/ticker"
        "${D_MODS}/module_a"
        "${D_MODS}/module_b"
        "${D_MODS}/module_sysmon"
        "${D_MODS}/module_spiffs"
        "${D_MODS}/module_config"
        "${D_MODS}/module_timer"
        "${D_MODS}/module_leds"
        "${D_MODS}/module_default_btn"
        "${D_MODS}/module_print_btn"
        "${D_MODS}/module_fuel"
        "${D_MODS}/module_fuel/pumps"
        "${D_MODS}/module_fuel/pumps/sanki"
        "${D_MODS}/module_buzzer"
        "${D_MODS}/module_cloud"
        "${D_MODS}/module_internet"
        "${D_MODS}/module_wifi"
        "${D_MODS}/module_sd"
        "${D_MODS}/module_timemgr"
        # App messages and peripherals
        "${D_MSGS}"
        "${D_PHER}"
        # Generic peripheral
        "${D_SUB}/peripheral"
        "${D_SUB}/peripheral/generic_peripheral"
        # Middleware
        "${D_SUB}/middleware"
        "${D_SUB}/middleware/list-manager"
        # PAL esp-idf impls + root for pal/pal_uart.h style includes
        "${D_SUB}/pal/esp-idf"
        "${D_SUB}"
        # DISTAP device library
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device"
        # DS1307 driver
        "${D_SUB}/drivers/DS1307"
        # CubeSphere cloud driver
        "${D_SUB}/drivers/cube_sphere"
        # hsys headers (needed by app.h and friends)
        "${D_SUB}/hsys-framework"
        "${D_SUB}/hsys-os"

    REQUIRES
        hsys-framework
        hsys-os
        pal
        bblanchon__arduinojson

    PRIV_REQUIRES
        log
        driver
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        spiffs
        fatfs
        sdmmc
        mqtt
        esp_http_client
        esp-tls
        lwip
        nvs_flash
        esp_system
        app_update
)

# ── Per-file compile-option overrides ─────────────────────────────────────────
set_source_files_properties(
    "${D_MODS}/module_fuel/module_fuel.cpp"
    "${D_MODS}/module_fuel/pumps/sanki/sanki_6_digit_1.cpp"
    "${D_MODS}/module_print_btn/module_print_btn.cpp"
    "${D_SUB}/drivers/DS1307/ds1307.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers;-Wno-type-limits"
)

set_source_files_properties(
    "${D_SUB}/drivers/cube_sphere/cube_sphere_api.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-missing-field-initializers"
)

set_source_files_properties(
    "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/board.c"
    "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/com_distap.c"
    "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/cmd_distap.c"
    PROPERTIES COMPILE_OPTIONS "-DBOARD_2404"
)

set_source_files_properties(
    "${D_SUB}/pal/esp-idf/pal_esp_idf_http_client.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_mqtt.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_wifi.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_fw_update.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_i2c.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_sd.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_uart.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers"
)
