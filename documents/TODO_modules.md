# TODO — Module Port to HSYS Message Queue Architecture

Legend: ✅ done · 🔶 partial · ❌ not started

## Modules

| # | Module | Old Name | Status | Notes |
|---|---|---|---|---|
| 1 | ModuleConfig | `app_config` | ✅ | 257 lines, 0 TODOs |
| 2 | ModuleWifi | `app_wifi` | ✅ | 216 lines, 0 TODOs |
| 3 | ModuleInternet | `app_internet` | ✅ | 190 lines, 0 TODOs |
| 4 | ModuleCloud | `app_cloud` | 🔶 | 379 lines; `set_driver()` not wired in `main.cpp`; `time_stamp=0` — needs `pal_time_get_unix()`; `fw_version` hardcoded `"1.0.0"` — needs `version.h`; `MsgRetransmitStore` publish stubbed at lines 181 & 208 (requires ModuleRetransmit) |
| 5 | ModuleMqtt | `app_mqtt` | ❌ | — |
| 6 | ModuleFuel | `app_fuel` + `app_disptap` | ✅ | 446 lines, 0 TODOs; display-tap logic integrated internally |
| 7 | ModuleRetransmit | `app_retransmit` | ❌ | Stubbed in `module_cloud.cpp` lines 181 & 208; needs `MSG_ID_RETRANSMIT_STORE` |
| 8 | ModuleOta | `app_ota` | ❌ | Three paths: web-upload (HTTP POST direct to driver), cloud-pull (300 s timer), MQTT-push (`MsgOtaTrigger`); binary data never enters message pool |
| 9 | ModuleWebServer | `app_webserver` | ❌ | — |
| 10 | ModuleTimeMgr | `app_time` | ✅ | 347 lines, 0 TODOs; `MsgTimeStatus::DESCRIPTOR` missing from `app_msg_table.h` |
| 11 | ModuleLeds | `app_leds` | 🔶 | 82 lines; only subscribes `MsgSpiffsReady` — add: `MsgWifiEvent`, `MsgInternetStatus`, `MsgNozzleState`, `MsgCloudStatus`, `MsgOtaEvent` |
| 12 | ModuleBuzzer | `app_buzzer` | 🔶 | 113 lines; missing `MsgNozzleState` and `MsgOtaEvent` subscriptions |
| 13 | ModulePrintBtn | `app_print_btn` | ✅ | 136 lines, 0 TODOs |
| 14 | ModuleDefaultBtn | `app_default_btn` | ✅ | 92 lines; long-press logs only — should publish `MsgConfigSet` to trigger factory reset |
| 15 | ModuleSpiffs | `app_spiffs` | 🔶 | 54 lines; mounts and publishes `MsgSpiffsReady`; no `MsgStorageWrite/Read` broker yet |
| 16 | ModuleSD | `app_sd` | 🔶 | 86 lines; mounts and publishes `MsgSdReady/Status`; no `MsgSdStorageRequest` broker yet |
| 17 | ModuleHw | `app_hw` | ❌ | GPIO/ADC/UART init not PAL-owned; publishes `MsgHwReady`; subscribes `MsgSystemReboot` for clean shutdown before reset |

## Cross-Cutting Tasks

| # | Task | Status |
|---|---|---|
| 1 | Add `MsgTimeStatus`, `MsgWifiEvent`, `MsgInternetStatus`, `MsgCloudStatus` to `app_msg_table.h` | ❌ |
| 2 | Wire `ModuleCloud::set_driver(cloud_driver_cube_sphere())` in `main.cpp` before `app_init()` | ❌ |
| 3 | Create `src/product/app/version.h` with `FW_VERSION_STRING` constant | ❌ |
| 4 | Add log-enable flags to `user_config.h` for all modules and PAL implementations | ❌ |
| 5 | Remove `event_table_t` and `fp_event_interface_t` from `app_common.h` once all modules ported | ❌ |
| 6 | Audit `while(1)` crash-on-null guards → replace with `log_error` + graceful degradation | ❌ |
