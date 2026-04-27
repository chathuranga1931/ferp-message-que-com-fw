# TODO — Message Definitions for HSYS Architecture

Legend: ✅ done · ❌ not started

## ID Space

| Range | Subsystem |
|---|---|
| 0x01xx | System / lifecycle |
| 0x02xx | Config |
| 0x03xx | WiFi |
| 0x04xx | Internet |
| 0x05xx | Cloud |
| 0x06xx | MQTT |
| 0x07xx | Fuel / Nozzle |
| 0x08xx | Print / Printer |
| 0x09xx | Buttons |
| 0x0Axx | OTA |
| 0x0Bxx | Time / NTP |
| 0x0Cxx | Storage (SPIFFS / SD) |
| 0x0Dxx | Web Server |
| 0x0Exx | Retransmission |

## Message Definitions

| ID | Class | Status | Publisher | Subscribers | Notes / Key Payload |
|---|---|---|---|---|---|
| 0x0101 | `MsgTick1000ms` | ✅ | Ticker | ModuleSysmon, ModuleCloud, ModuleRetransmit, ModuleOta | No payload; 1 s heartbeat |
| 0x0102 | `MsgSystemReboot` | ❌ | ModuleOta, ModuleDefaultBtn | ModuleHw, all | `reason`: OTA_COMPLETE, CONFIG_RESET, WATCHDOG |
| 0x0103 | `MsgHwReady` | ❌ | ModuleHw | ModuleWifi, ModuleSpiffs, ModuleSd | No payload |
| 0x0201 | `MsgConfigLoaded` | ❌ | ModuleConfig | ModuleWifi, ModuleCloud, ModuleMqtt, ModulePrinter | Full `app_config_t` snapshot |
| 0x0202 | `MsgConfigUpdateRequest` | ❌ | ModuleWebServer, ModuleDefaultBtn | ModuleConfig | `key` (string), `value` (bytes + type), `timeout_ms` |
| 0x0203 | `MsgConfigResetRequest` | ❌ | ModuleDefaultBtn | ModuleConfig | No payload; triggers factory-default reload |
| 0x0301 | `MsgWifiEvent` | ❌ | ModuleWifi | ModuleInternet, ModuleCloud, ModuleMqtt, ModuleWebServer, ModuleLeds | `app_wifi_event_t event_id`; `uint8_t ip[4]` (GOT_IP); `int8_t rssi` (RSSI_CHANGED); events: STA_START/CONNECTED/DISCONNECTED/GOT_IP, AP_START/STOP, AP_STA_CONNECTED/DISCONNECTED, RSSI_CHANGED, NO_AVAILABLE_SIGNAL |
| 0x0401 | `MsgInternetStatus` | ❌ | ModuleInternet | ModuleCloud, ModuleMqtt, ModuleOta, ModuleTime, ModuleLeds | `bool connected` |
| 0x0501 | `MsgCloudStatus` | ❌ | ModuleCloud | ModuleLeds, ModuleRetransmit, ModuleBuzzer | `app_cloud_event_t event_id`: CONFIG_READY, CONFIG_FAILED_RETRYING, PUMPED_SUCCESS, PUMPED_FAILED |
| 0x0601 | `MsgMqttEvent` | ❌ | ModuleMqtt | ModuleLeds, ModuleOta | `pal_mqtt_event_t event_id`: CONNECTED, DISCONNECTED, SUBSCRIBED |
| 0x0602 | `MsgMqttRxMessage` | ❌ | ModuleMqtt | topic-registered handlers | `topic[MAX_MQTT_TOPIC_LENGTH]`, `payload[MAX_MQTT_PAYLOAD_LENGTH]`, `payload_len`, `qos`, `dup`, `retain` |
| 0x0603 | `MsgMqttPublishRequest` | ❌ | any | ModuleMqtt | `topic[MAX_MQTT_TOPIC_LENGTH]`, `payload[MAX_MQTT_PAYLOAD_LENGTH]`, `payload_len`, `qos`, `retain` |
| 0x0701 | `MsgDispTapData` | ❌ | ModuleDispTap | ModuleFuel | `nozzle_idx`, `frame[DISPLAY_TAP_FRAME_MAX_SIZE]`, `frame_len`; crosses task boundary from co-processor |
| 0x0702 | `MsgNozzleState` | ❌ | ModuleFuel | ModuleLeds, ModuleBuzzer, ModuleCloud | `nozzle_idx`, `app_pump_event_t state` (STARTED/STOPPED), `timestamp_ms`; published once at start and once at stop only |
| 0x0703 | `MsgFuelPumped` | ❌ | ModuleFuel | ModuleCloud, ModulePrinter, ModuleRetransmit | `nozzle_idx`, `fuel_type`, `timestamp_ms`, `unit_pricex100`, `total_pricex100`, `volume_lx1000`; published once on confirmed nozzle stop with valid data |
| 0x0704 | `MsgDispTapFwVersion` | ❌ | ModuleDispTap | ModuleOta | `nozzle_idx`, `fw_version[16]`; marks esp07 OTA driver ready |
| 0x0801 | `MsgPrintBtnEvent` | ❌ | ModulePrintBtn | ModulePrinter, ModuleBuzzer | `app_print_btn_event_t event_id`: PRINT1_SHORT, PRINT1_LONG, PRINT2_SHORT, PRINT2_LONG |
| 0x0802 | `MsgPrintRequest` | ❌ | any | ModulePrinter | `nozzle_idx`, `nozzle_id[SIZE_OF_NOZZELID]`, `fuel_type_str`, `unit_pricex100`, `total_pricex100`, `volume_lx1000`, `timestamp_ms`, `copy_count` |
| 0x0803 | `MsgPrinted` | ❌ | ModulePrinter | ModuleCloud, ModuleRetransmit | `nozzle_idx`, `bool success`, `int32_t error_code` |
| 0x0901 | `MsgDefaultBtnEvent` | ❌ | ModuleDefaultBtn | ModuleConfig, ModuleLeds | `app_default_btn_event_t event_id`: SHORT_PRESS, LONG_PRESS |
| 0x0A01 | `MsgOtaEvent` | ❌ | ModuleOta | ModuleLeds, ModuleBuzzer, ModuleCloud | `app_ota_event_t event_id`: CHECK_STARTED/SUCCESS, DOWNLOAD_STARTED/SUCCESS/FAILURE; `driver_index` (0=esp32-main, 1=esp07); `version[32]`; on DOWNLOAD_SUCCESS module calls `pal_power_reset()` after short delay |
| 0x0A02 | `MsgOtaTrigger` | ❌ | ModuleMqtt | ModuleOta | `driver_index`, `version[32]` (empty = check latest); MQTT-push control only — binary data never enters message pool |
| 0x0B01 | `MsgTimeStatus` | ❌ | ModuleTime | ModuleCloud, ModuleMqtt, ModuleLeds | `app_timeman_event_t event_id`: READY, UPDATED_FROM_RTC, UPDATED_FROM_NTP, UPDATED_FROM_BACKUP, UPDATE_FAILED_CRITICAL; `time_t epoch` |
| 0x0C01 | `MsgStorageRequest` | ❌ | any | ModuleSpiffs / ModuleSd | `op` (READ/WRITE/APPEND/DELETE/CREATE), `medium` (SPIFFS=0, SD=1), `path[64]`, `data[256]`, `data_len`, `requester_id`, `request_token` |
| 0x0C02 | `MsgStorageResult` | ❌ | ModuleSpiffs / ModuleSd | requester | `request_token`, `bool success`, `int32_t error_code`, `data[256]`, `data_len` |
| 0x0C03 | `MsgSpiffsReady` | ❌ | ModuleSpiffs | ModuleConfig, ModuleTime | No payload |
| 0x0C04 | `MsgSdReady` | ❌ | ModuleSd | ModuleRetransmit | No payload |
| 0x0D01 | `MsgWebServerEvent` | ❌ | ModuleWebServer | ModuleLeds | `app_webserver_event_t event_id`: STARTED, CONFIG_UPDATED |
| 0x0E01 | `MsgRetransmitStatus` | ❌ | ModuleRetransmit | ModuleLeds | `pending_count`, `bool last_retry_ok`, `int32_t last_error_code` |
