# =============================================================================
# FERP receipt printer — USB model (ESP32-S3 USB host): all application sources.
# Path roots relative to this file (src/product/ferp-printer-usb-v1/ferp-printer-usb-v1-esp32s3-idf/main/).
# =============================================================================
set(D_APP  "${CMAKE_CURRENT_SOURCE_DIR}/../../app")
set(D_MODS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../app-modules")
set(D_MSGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../app-messages")
set(D_PHER "${CMAKE_CURRENT_SOURCE_DIR}/../../../../app-pheripherals")
set(D_SUB  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../sub-modules")

idf_component_register(
    SRCS
        # ── Entry point + application ─────────────────────────────────────
        "main.cpp"
        "${D_APP}/app.cpp"

        # ── App modules ───────────────────────────────────────────────────
        "${D_MODS}/ticker/ticker.cpp"
        "${D_MODS}/module_sysmon/module_sysmon.cpp"
        "${D_MODS}/module_spiffs/module_spiffs.cpp"
        "${D_MODS}/module_config/module_config.cpp"
        "${D_MODS}/module_timer/module_timer.cpp"
        "${D_MODS}/module_internet/module_internet.cpp"
        "${D_MODS}/module_wifi/module_wifi.cpp"
        "${D_MODS}/module_ota/OtaModule.cpp"
        "${D_MODS}/module_ota/targets/ota_driver_esp32_main.c"
        "${D_MODS}/module_device_info/module_device_info.cpp"
        "${D_MODS}/module_web_server/ModuleWebServer.cpp"
        "${D_MODS}/module_mqtt/ModuleMqtt.cpp"
        "${D_MODS}/module_web_client_ota/ModuleWebClientOta.cpp"
        "${D_MODS}/module_web_client_ota/hsys_ota.cpp"
        "${D_MODS}/module_udp_log/ModuleUdpLog.cpp"
        "${D_MODS}/module_http/module_http.cpp"

        # ── Receipt printer ───────────────────────────────────────────────
        "${D_MODS}/module_printer/ModulePrinter.cpp"
        "${D_MODS}/module_printer/ModulePrinterApi.cpp"
        "${D_MODS}/module_printer/receipt_renderer.cpp"
        "${D_MODS}/module_printer/printer_transport_usb.cpp"
        "${D_MODS}/module_cloud_print/ModuleCloudPrint.cpp"

        # ── Utilities ─────────────────────────────────────────────────────
        "${D_SUB}/utils/crc32.cpp"

        # ── App messages ──────────────────────────────────────────────────
        "${D_MSGS}/app_msg_codec.cpp"
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
        "${D_MSGS}/configs/msg_config_ready.cpp"
        "${D_MSGS}/configs/msg_config_set.cpp"
        "${D_MSGS}/configs/msg_config_get.cpp"
        "${D_MSGS}/configs/msg_config_get_key.cpp"
        "${D_MSGS}/configs/msg_config_value.cpp"
        "${D_MSGS}/configs/msg_config_updated.cpp"
        "${D_MSGS}/wifi/msg_config_get_wifi.cpp"
        "${D_MSGS}/wifi/msg_config_wifi.cpp"
        "${D_MSGS}/wifi/msg_wifi_event.cpp"
        "${D_MSGS}/cloud/msg_config_get_cloud.cpp"
        "${D_MSGS}/cloud/msg_config_cloud.cpp"
        "${D_MSGS}/cloud/msg_internet_status.cpp"
        "${D_MSGS}/cloud/msg_cubesphere_status.cpp"
        "${D_MSGS}/mqtt/msg_config_get_mqtt.cpp"
        "${D_MSGS}/mqtt/msg_config_mqtt.cpp"
        "${D_MSGS}/mqtt/msg_mqtt_status.cpp"
        "${D_MSGS}/mqtt/msg_mqtt_subscribe.cpp"
        "${D_MSGS}/mqtt/msg_mqtt_ext_data.cpp"
        "${D_MSGS}/timer/msg_tick_1000ms.cpp"
        "${D_MSGS}/timer/msg_timer_start.cpp"
        "${D_MSGS}/timer/msg_timer_stop.cpp"
        "${D_MSGS}/timer/msg_timer_start_response.cpp"
        "${D_MSGS}/timer/msg_timer_stop_response.cpp"
        "${D_MSGS}/timer/msg_timer_alarm.cpp"
        "${D_MSGS}/timer/msg_time_status.cpp"
        "${D_MSGS}/buttons/msg_default_btn.cpp"
        "${D_MSGS}/fuel/msg_config_get_dt.cpp"
        "${D_MSGS}/fuel/msg_config_dt.cpp"
        "${D_MSGS}/fuel/msg_fuel_pumped.cpp"
        "${D_MSGS}/fuel/msg_nozzle_state.cpp"
        "${D_MSGS}/fuel/msg_printer_btn.cpp"
        "${D_MSGS}/fuel/msg_fuel_print_status.cpp"
        "${D_MSGS}/fuel/msg_totalizer_data.cpp"
        "${D_MSGS}/fuel/msg_fuel_totalizer.cpp"
        "${D_MSGS}/deviceInfo/msg_dev_info_read.cpp"
        "${D_MSGS}/deviceInfo/msg_dev_info_write.cpp"
        "${D_MSGS}/deviceInfo/msg_dev_info_value.cpp"
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
        "${D_MSGS}/http/msg_http_request.cpp"
        "${D_MSGS}/http/msg_http_result.cpp"
        "${D_MSGS}/http/msg_http_response_header.cpp"
        "${D_MSGS}/printer/msg_print_job.cpp"
        "${D_MSGS}/printer/msg_print_result.cpp"
        "${D_MSGS}/printer/msg_printer_cmd.cpp"
        "${D_MSGS}/printer/msg_printer_get_status.cpp"
        "${D_MSGS}/printer/msg_printer_status.cpp"
        "${D_MSGS}/printer/msg_cloud_print_get_status.cpp"
        "${D_MSGS}/printer/msg_cloud_print_status.cpp"

        # ── App peripherals ───────────────────────────────────────────────
        "${D_PHER}/app_spiffs.cpp"
        "${D_PHER}/app_nvs.cpp"

        # ── Middleware ────────────────────────────────────────────────────
        "${D_SUB}/middleware/hsys_config.cpp"
        "${D_SUB}/middleware/hsys_device_info.cpp"

        # ── PAL ESP-IDF implementations ───────────────────────────────────
        "${D_SUB}/pal/esp-idf/pal_esp_idf_time.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_logger.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_logger_uart.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_spiffs.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_gpio.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_system.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_network.cpp"
        "${D_SUB}/pal/esp-idf/pal_esp_idf_wifi.cpp"
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
    INCLUDE_DIRS
        "."
        "${D_APP}"
        "${D_MODS}"
        "${D_MODS}/ticker"
        "${D_MODS}/module_sysmon"
        "${D_MODS}/module_spiffs"
        "${D_MODS}/module_config"
        "${D_MODS}/module_timer"
        "${D_MODS}/module_internet"
        "${D_MODS}/module_wifi"
        "${D_MODS}/module_ota"
        "${D_MODS}/module_ota/targets"
        "${D_MODS}/module_web_server"
        "${D_MODS}/module_mqtt"
        "${D_MODS}/module_web_client_ota"
        "${D_MODS}/module_device_info"
        "${D_MODS}/module_http"
        "${D_MODS}/module_udp_log"
        "${D_MODS}/module_printer"
        "${D_MODS}/module_cloud_print"
        "${D_MODS}/module_fuel/pumps"          # nozzle_event.h (header-only, used by fuel message types)
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
        "${D_MSGS}/printer"
        "${D_PHER}"
        "${D_SUB}/peripheral"
        "${D_SUB}/middleware"
        "${D_SUB}/utils"
        "${D_SUB}/pal/esp-idf"
        "${D_SUB}"
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
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        spiffs
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
        espressif__usb
)

set_source_files_properties(
    "${D_SUB}/pal/esp-idf/pal_esp_idf_http_client.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_mqtt.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_wifi.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_fw_update.cpp"
    "${D_SUB}/pal/esp-idf/pal_esp_idf_uart.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers"
)

# Panic handler intercept — see ferp-com-v3 main/CMakeLists.txt.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_panic_handler" APPEND)
