# =============================================================================
# All application sources for the ESP-IDF build.
# Path roots relative to this file (src/product/ferp-com-esp32-idf/main/).
# =============================================================================
set(D_APP  "${CMAKE_CURRENT_SOURCE_DIR}/../../app")
set(D_MODS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../app-modules")
set(D_MSGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../app-messages")
set(D_PHER "${CMAKE_CURRENT_SOURCE_DIR}/../../../../app-pheripherals")
set(D_SUB  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../sub-modules")

idf_component_register(
    SRCS
        # ── Entry point ───────────────────────────────────────────────────
        "main.cpp"

        # ── Application ───────────────────────────────────────────────────
        "${D_APP}/app.cpp"
        "${D_APP}/message_translater_support.cpp"

        # ── App modules ───────────────────────────────────────────────────
        "${D_MODS}/ticker/ticker.cpp"
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
        "${D_MODS}/module_fuel/pumps/censtar/censtar_6_digit_1.cpp"
        "${D_MODS}/module_fuel/pumps/censtar/censtar_7_digit_1.cpp"
        "${D_MODS}/module_fuel/pumps/wayne/wayne_6_digit_1.cpp"
        "${D_MODS}/module_fuel/pumps/wayne/wayne_6_digit_2.cpp"
        "${D_MODS}/module_fuel/pumps/longfeng/longfeng_8_digit_1.cpp"
        "${D_MODS}/module_fuel/pumps/hongyang/hongyang_8_digit_1.cpp"
        "${D_MODS}/module_buzzer/module_buzzer.cpp"
        "${D_MODS}/module_cubesphere/module_cubesphere.cpp"
        "${D_MODS}/module_internet/module_internet.cpp"
        "${D_MODS}/module_wifi/module_wifi.cpp"
        "${D_MODS}/module_sd/module_sd.cpp"
        "${D_MODS}/module_timemgr/module_timemgr.cpp"
        "${D_MODS}/module_ota/OtaModule.cpp"
        "${D_MODS}/module_ota/targets/ota_driver_esp32_main.c"
        "${D_MODS}/module_ota/targets/ota_driver_esp07_dt.c"

        # ── Device info module ────────────────────────────────────────────
        "${D_MODS}/module_device_info/module_device_info.cpp"

        # ── HTTP configuration / OTA web server (common, PAL-backed) ──────
        "${D_MODS}/module_web_server/ModuleWebServer.cpp"
        # ── MQTT broker client (common, PAL-backed) ───────────────────────
        "${D_MODS}/module_mqtt/ModuleMqtt.cpp"

        # ── Cloud-polling OTA source module ───────────────────────────────
        "${D_MODS}/module_web_client_ota/ModuleWebClientOta.cpp"
        "${D_MODS}/module_web_client_ota/hsys_ota.cpp"

        # ── Persistent log module ────────────────────────────────────────────
        "${D_MODS}/module_plog/module_plog.cpp"

        # ── Message translator module ─────────────────────────────────────────
        "${D_MODS}/module_msg_translator/ModuleMsgTranslator.cpp"

        # ── UDP log module ────────────────────────────────────────────────────────
        "${D_MODS}/module_udp_log/ModuleUdpLog.cpp"

        # ── HTTP client module ────────────────────────────────────────────────
        "${D_MODS}/module_http/module_http.cpp"

        # ── Print client module ──────────────────────────────────────────────────
        "${D_MODS}/module_printing/module_printing.cpp"

        # ── Utilities ────────────────────────────────────────────────────
        "${D_SUB}/utils/crc32.cpp"

        # ── App messages ──────────────────────────────────────────────────
        "${D_MSGS}/app_msg_codec.cpp"
        # System messages
        "${D_MSGS}/system/msg_spiffs_ready.cpp"
        "${D_MSGS}/system/msg_sd_ready.cpp"
        "${D_MSGS}/system/msg_sd_status.cpp"
        "${D_MSGS}/system/msg_persist_log.cpp"
        "${D_MSGS}/system/msg_system_status.cpp"
        "${D_MSGS}/system/msg_pool_get_json.cpp"
        "${D_MSGS}/system/msg_pool_json.cpp"
        "${D_MSGS}/system/msg_get_file_list_spiffs.cpp"
        "${D_MSGS}/system/msg_file_list_spiffs.cpp"
        "${D_MSGS}/system/msg_system_reboot.cpp"
        "${D_MSGS}/system/msg_sd_cleanup.cpp"
        "${D_MSGS}/system/msg_spiffs_cleanup.cpp"
        # Config messages
        "${D_MSGS}/configs/msg_config_ready.cpp"
        "${D_MSGS}/configs/msg_config_set.cpp"
        "${D_MSGS}/configs/msg_config_get.cpp"
        "${D_MSGS}/configs/msg_config_get_key.cpp"
        "${D_MSGS}/configs/msg_config_value.cpp"
        "${D_MSGS}/configs/msg_config_updated.cpp"
        # WiFi messages
        "${D_MSGS}/wifi/msg_config_get_wifi.cpp"
        "${D_MSGS}/wifi/msg_config_wifi.cpp"
        "${D_MSGS}/wifi/msg_wifi_event.cpp"
        # Cloud messages
        "${D_MSGS}/cloud/msg_config_get_cloud.cpp"
        "${D_MSGS}/cloud/msg_config_cloud.cpp"
        "${D_MSGS}/cloud/msg_internet_status.cpp"
        "${D_MSGS}/cloud/msg_cubesphere_status.cpp"
        # MQTT messages
        "${D_MSGS}/mqtt/msg_config_get_mqtt.cpp"
        "${D_MSGS}/mqtt/msg_config_mqtt.cpp"
        "${D_MSGS}/mqtt/msg_mqtt_status.cpp"
        # Timer messages
        "${D_MSGS}/timer/msg_tick_1000ms.cpp"
        "${D_MSGS}/timer/msg_timer_start.cpp"
        "${D_MSGS}/timer/msg_timer_stop.cpp"
        "${D_MSGS}/timer/msg_timer_start_response.cpp"
        "${D_MSGS}/timer/msg_timer_stop_response.cpp"
        "${D_MSGS}/timer/msg_timer_alarm.cpp"
        "${D_MSGS}/timer/msg_time_status.cpp"
        # Button messages
        "${D_MSGS}/buttons/msg_default_btn.cpp"
        # Fuel messages
        "${D_MSGS}/fuel/msg_config_get_dt.cpp"
        "${D_MSGS}/fuel/msg_config_dt.cpp"
        "${D_MSGS}/fuel/msg_fuel_pumped.cpp"
        "${D_MSGS}/fuel/msg_nozzle_state.cpp"
        "${D_MSGS}/fuel/msg_printer_btn.cpp"
        "${D_MSGS}/fuel/msg_fuel_print_status.cpp"
        "${D_MSGS}/fuel/msg_totalizer_data.cpp"
        "${D_MSGS}/fuel/msg_fuel_totalizer.cpp"
        # Device info messages
        "${D_MSGS}/deviceInfo/msg_dev_info_read.cpp"
        "${D_MSGS}/deviceInfo/msg_dev_info_write.cpp"
        "${D_MSGS}/deviceInfo/msg_dev_info_value.cpp"
        # OTA messages
        "${D_MSGS}/ota/msg_config_get_ota.cpp"
        "${D_MSGS}/ota/msg_config_ota.cpp"
        "${D_MSGS}/ota/msg_ota_start_request.cpp"
        "${D_MSGS}/ota/msg_ota_start_response.cpp"
        "${D_MSGS}/ota/msg_ota_request_driver.cpp"
        "${D_MSGS}/ota/msg_ota_driver_response.cpp"
        "${D_MSGS}/ota/msg_ota_abort_request.cpp"
        "${D_MSGS}/ota/msg_ota_complete_notify.cpp"
        "${D_MSGS}/ota/msg_ota_event.cpp"
        "${D_MSGS}/ota/msg_ota_progress.cpp"
        # HTTP session messages
        "${D_MSGS}/http/msg_http_request.cpp"
        "${D_MSGS}/http/msg_http_result.cpp"
        "${D_MSGS}/http/msg_http_response_header.cpp"

        # ── App peripherals ───────────────────────────────────────────────
        "${D_PHER}/app_spiffs.cpp"
        "${D_PHER}/app_sd.cpp"
        "${D_PHER}/app_nvs.cpp"

        # ── Generic peripheral implementations ────────────────────────────
        "${D_SUB}/peripheral/generic_peripheral/hsys_led.cpp"
        "${D_SUB}/peripheral/generic_peripheral/hsys_buzzer.cpp"
        "${D_SUB}/peripheral/generic_peripheral/hsys_button.cpp"
        "${D_SUB}/peripheral/generic_peripheral/hsys_tog_button.cpp"

        # ── Middleware ────────────────────────────────────────────────────
        "${D_SUB}/middleware/hsys_config.cpp"
        "${D_SUB}/middleware/hsys_device_info.cpp"
        "${D_SUB}/middleware/list-manager/list_manager.cpp"
        "${D_SUB}/middleware/retransmission-manager/retransmission_manager.cpp"
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
        "${D_SUB}/pal/esp-idf/pal_esp_idf_http_server.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_uart.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_efuse.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_crypto.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_fw_update.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_udp_log.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_nvs.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_crash_log.cpp"

        # ── DISTAP device library (protocol only, from ferp_board) ───────
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/board.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/com_distap.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device/cmd_distap.c"

        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher/serial_flasher.cpp"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher/esp_loader.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher/esp32_port.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher/esp_targets.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher/md5_hash.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher/protocol_common.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher/protocol_uart.c"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher/slip.c"

        # ── External drivers ──────────────────────────────────────────────
        "${D_SUB}/drivers/DS1307/ds1307.cpp"
    INCLUDE_DIRS
        # Entry point + app
        "."
        "${D_APP}"
        # App modules
        "${D_MODS}"
        "${D_MODS}/ticker"
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
        "${D_MODS}/module_fuel/pumps/censtar"
        "${D_MODS}/module_fuel/pumps/wayne"
        "${D_MODS}/module_fuel/pumps/longfeng"
        "${D_MODS}/module_fuel/pumps/hongyang"
        "${D_MODS}/module_buzzer"
        "${D_MODS}/module_cubesphere"
        "${D_MODS}/module_internet"
        "${D_MODS}/module_wifi"
        "${D_MODS}/module_sd"
        "${D_MODS}/module_timemgr"
        "${D_MODS}/module_ota"
        "${D_MODS}/module_ota/targets"
        "${D_MODS}/module_web_server"
        "${D_MODS}/module_mqtt"
        "${D_MODS}/module_web_client_ota"
        "${D_MODS}/module_device_info"
        "${D_MODS}/module_plog"
        "${D_MODS}/module_http"
        "${D_MODS}/module_msg_translator"
        "${D_MODS}/module_udp_log"
        "${D_MODS}/module_printing"
        # App messages and peripherals
        "${D_MSGS}"
        "${D_MSGS}/buttons"
        "${D_MSGS}/cloud"
        "${D_MSGS}/configs"
        "${D_MSGS}/fuel"
        "${D_MSGS}/mqtt"
        "${D_MSGS}/ota"
        "${D_MSGS}/system"
        "${D_MSGS}/timer"
        "${D_MSGS}/wifi"
        "${D_MSGS}/deviceInfo"
        "${D_MSGS}/http"
        "${D_PHER}"
        # Generic peripheral
        "${D_SUB}/peripheral"
        "${D_SUB}/peripheral/generic_peripheral"
        # Middleware
        "${D_SUB}/middleware"
        "${D_SUB}/middleware/list-manager"
        "${D_SUB}/middleware/retransmission-manager"
        "${D_SUB}/utils"
        # PAL esp-idf impls + root for pal/pal_uart.h style includes
        "${D_SUB}/pal/esp-idf"
        "${D_SUB}"
        # DISTAP device library (pin defs in board_inf.h also used by com_distap)
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/device"
        "${D_SUB}/ferp-device-firmware/ferp_board/main-esp32/lib/serial_flasher"
        # DS1307 driver
        "${D_SUB}/drivers/DS1307"
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
        esp_driver_uart
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_driver_sdspi
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        spiffs
        fatfs
        sdmmc
        espressif__mqtt
        esp_http_client
        esp_http_server
        esp-tls
        lwip
        nvs_flash
        esp_system
        app_update
        mbedtls
        esp_hal_wdt
)

# ── Per-file compile-option overrides ─────────────────────────────────────────
set_source_files_properties(
    "${D_MODS}/module_fuel/module_fuel.cpp"
    "${D_MODS}/module_fuel/pumps/sanki/sanki_6_digit_1.cpp"
    "${D_MODS}/module_fuel/pumps/censtar/censtar_6_digit_1.cpp"
    "${D_MODS}/module_fuel/pumps/censtar/censtar_7_digit_1.cpp"
    "${D_MODS}/module_fuel/pumps/wayne/wayne_6_digit_1.cpp"
    "${D_MODS}/module_fuel/pumps/wayne/wayne_6_digit_2.cpp"
    "${D_MODS}/module_fuel/pumps/longfeng/longfeng_8_digit_1.cpp"
    "${D_MODS}/module_fuel/pumps/hongyang/hongyang_8_digit_1.cpp"
    "${D_MODS}/module_print_btn/module_print_btn.cpp"
    "${D_SUB}/drivers/DS1307/ds1307.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers;-Wno-type-limits"
)

# Note: BOARD_2602 is defined globally via add_compile_definitions() in the
# top-level CMakeLists.txt. No per-file board override needed for distap files.

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

# ── Panic handler intercept ───────────────────────────────────────────────────
# Wrap esp_panic_handler so __wrap_esp_panic_handler in pal_esp_idf_crash_log.cpp
# runs first (captures crash data to RTC SRAM), then calls __real_esp_panic_handler
# which is the unmodified IDF implementation (full UART output + reset).
#
# idf_build_set_property(LINK_OPTIONS) is used instead of target_link_options PRIVATE.
# target_link_options PRIVATE on a component static library is NOT propagated to the
# final elf link step in IDF's CMake build system — the --wrap flag is silently
# dropped and __wrap_esp_panic_handler becomes dead code.
# idf_build_set_property injects directly into the global linker command that
# produces the .elf, guaranteeing --wrap is present when all objects are combined.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_panic_handler" APPEND)
