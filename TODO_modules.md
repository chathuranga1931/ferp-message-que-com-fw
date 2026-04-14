# TODO — Module Port to HSYS Message Queue Architecture

Each old component used a flat `event_table_t` of raw function pointers and passed
data as `void *` arg. The new architecture gives every component its own
`HsysModule` subclass living in a dedicated FreeRTOS task, communicating only
through typed, ref-counted messages on the queue.

Legend: `[ ]` = not started · `[~]` = in progress · `[x]` = done

---

## 1. ModuleConfig  *(was `app_config`)*

- [ ] Port `app_config_t` (wifi, app settings, printer, mqtt, logging) to a config-store module
- [ ] On init: load config from SPIFFS/NVS, publish `MsgConfigLoaded` with snapshot
- [ ] On `MsgConfigUpdateRequest`: save new value, re-publish `MsgConfigLoaded`
- [ ] Consumers subscribe to `MsgConfigLoaded` (wifi, cloud, mqtt, printer…)
- [ ] No more global `_app_config`; config is owned by this module

---

## 2. ModuleWifi  *(was `app_wifi`)*

- [ ] Wrap PAL wifi driver; subscribe to `MsgConfigLoaded` for SSID / password
- [ ] Publish `MsgWifiEvent` for every PAL wifi state change
  (STA_START, STA_CONNECTED, STA_DISCONNECTED, STA_GOT_IP, AP_START, AP_STOP,
   AP_STA_CONNECTED, AP_STA_DISCONNECTED, STA_RSSI_CHANGED, NO_AVAILABLE_SIGNAL)
- [ ] Internal soft-timer for RSSI polling / reconnect back-off
- [ ] Replace `_on_config_event` callback with subscription to `MsgConfigLoaded`

---

## 3. ModuleInternet  *(was `app_internet`)*

- [ ] Subscribe to `MsgWifiEvent` (GOT_IP → connected, DISCONNECTED → disconnected)
- [ ] Perform periodic HTTP reachability check (ping / HEAD request)
- [ ] Publish `MsgInternetStatus` (CONNECTED / DISCONNECTED)
- [ ] Consumers: ModuleCloud, ModuleMqtt, ModuleOta

---

## 4. ModuleCloud  *(was `app_cloud`)*

- [ ] Subscribe to `MsgWifiEvent`, `MsgInternetStatus`, `MsgFuelPumped`, `MsgPrinted`
- [ ] On internet connected: send startup request, then periodic heartbeat
- [ ] On `MsgFuelPumped`: call cloud driver `fp_on_cloud_event_pumped_rqst`
- [ ] On `MsgPrinted`: call cloud driver `fp_on_cloud_event_printed_rqst`
- [ ] Publish `MsgCloudStatus` (CONFIG_READY, CONFIG_FAILED, PUMPED_SUCCESS, PUMPED_FAILED)
- [ ] Replace internal `hsys_eventgroup` + `hsys_queue` with HSYS message queue
- [ ] `cloud_driver_t` stays as an injected interface (dependency injection via init struct)

---

## 5. ModuleMqtt  *(was `app_mqtt`)*

- [ ] Subscribe to `MsgWifiEvent` (GOT_IP → connect broker), `MsgInternetStatus`
- [ ] Subscribe to `MsgConfigLoaded` for broker URI / port / topic table
- [ ] Publish `MsgMqttEvent` (CONNECTED, DISCONNECTED, MSG_RECEIVED)
- [ ] Carry inbound payload in `MsgMqttRxMessage` (topic + payload buffer)
- [ ] Provide `app_mqtt_publish()` equivalent via `MsgMqttPublishRequest`
- [ ] Topic-to-handler table stays; handlers become message publishers

---

## 6. ModuleFuel  *(was `app_fuel` + `app_disptap`)*

> Core domain module — nozzle start/stop capture and display-tap decoding.
>
> ### Internal vs external events
>
> The old code exposed `on_nozzle1_start` / `on_nozzle2_start` callbacks
> because the flat `hsys_taskrunner` ran every module from the same loop and
> needed cross-module function pointers.  In the new architecture
> `ModuleFuel` **owns** both the `hsys_tog_button` debouncers and the Sanki
> state machine.  They execute inside `ModuleFuel`'s task and communicate only
> through private member variables — **no messages for intra-module state**.
>
> A message is only sent when a **completed transaction result** must be
> delivered to other tasks (cloud, printer, retransmit, LEDs, buzzer).

- [ ] Subscribe to `MsgDispTapData` (raw display-tap frames from `ModuleDispTap`)
- [ ] Run Sanki 6-digit state machine **internally** on each received frame
- [ ] Drive `hsys_tog_button` debouncers **internally** — no nozzle-start/stop messages to self
- [ ] On nozzle start transition (internal): update member state, start accumulation
- [ ] On nozzle stop + valid complete data: publish `MsgFuelPumped` (single message, one per transaction)
- [ ] On nozzle start/stop state change: publish `MsgNozzleState` (for LEDs/buzzer/cloud status only)
- [ ] All intermediate display-tap values (unit price, running volume, total) are plain member variables
- [ ] Replace per-nozzle `hsys_queue` with typed `MsgDispTapData` messages (task boundary only)

---

## 7. ModuleDispTap  *(was `app_disptap`)*

- [ ] Hardware driver module: reads Sanki display-tap serial lines (UART/GPIO)
- [ ] On each complete frame: publish `MsgDispTapData` (nozzle index, raw frame bytes)
- [ ] Publish `MsgDispTapFwVersion` when firmware version string is loaded from tap
- [ ] Decoupled from fuel logic; ModuleFuel consumes `MsgDispTapData`

---

## 8. ModuleRetransmit  *(was `app_retransmit` + `retransmission_manager`)*

- [ ] Subscribe to `MsgFuelPumped`, `MsgPrinted` to detect events that need cloud ACK
- [ ] Subscribe to `MsgCloudStatus` to know when cloud is reachable for retry
- [ ] On cloud NACK / timeout: persist event to SD/SPIFFS via `MsgStorageWrite`
- [ ] Periodic retry: publish `MsgRetransmitRetry` with serialised `retx_event_t`
- [ ] Publish `MsgRetransmitStatus` (pending count, last retry result)

---

## 9. ModuleOta  *(was `app_ota`)*

> ### Three OTA paths — all execute inside ModuleOta's task
>
> | Path | Wake source | How binary arrives |
> |---|---|---|
> | **Web-upload** | HTTP upload callback (synchronous, no wake message) | `ModuleWebServer` calls `hsys_ota_driver_t::fw_drv->fp_write()` directly |
> | **Cloud-pull** | `MsgInternetStatus` CONNECTED + internal soft-timer (300 s) | ModuleOta downloads via `fp_download_and_flash()` |
> | **MQTT-push** | `MsgOtaTrigger` (small JSON control message) | Same cloud-pull path, triggered immediately |
>
> **No OTA binary data crosses the message bus.**  Only the small
> control/status messages (`MsgOtaEvent`, `MsgOtaTrigger`) use the queue.
> Binary chunks are handled synchronously inside the PAL flash-writer or the
> HTTP upload callback — adding a queue hop would waste RAM (large pool blocks)
> and add latency to the write stream.

- [ ] Maintain array of `hsys_ota_driver_t*` (one per firmware target: esp32-main, esp07-coprocessor)
- [ ] Each driver has its own state machine: WAIT_INTERNET → CHECKING → DOWNLOAD_PENDING → DOWNLOADING
- [ ] Subscribe to `MsgInternetStatus` (CONNECTED → start 300 s timer + immediate check)
- [ ] Subscribe to `MsgOtaTrigger` (MQTT-push path → set TIMER_FIRED bit immediately)
- [ ] Subscribe to `MsgDispTapFwVersion` (sets esp07 driver "is_ready", same as `app_ota_on_driver_ready()`)
- [ ] Subscribe to `MsgSpiffsReady` (sets esp32-main driver "is_ready", same as `app_ota_on_driver_ready()`)
- [ ] Publish `MsgOtaEvent` on state transitions (CHECK_STARTED, DOWNLOAD_STARTED, DOWNLOAD_SUCCESS, DOWNLOAD_FAILURE)
- [ ] On DOWNLOAD_SUCCESS: publish `MsgOtaEvent`, wait ~500 ms, call `pal_power_reset()`
- [ ] Web-upload path: register `hsys_ota_driver_t::fw_drv` callbacks at init; `ModuleWebServer` calls them directly with no queue involvement
- [ ] esp07 "binary" is staged to SPIFFS (same as old `_esp07dt_fw_begin/write/end`); only the esp32-main binary writes directly to the OTA partition

---

## 10. ModuleWebServer  *(was `app_webserver`)*

- [ ] Subscribe to `MsgWifiEvent` (AP_START → start HTTP server; AP_STOP → stop)
- [ ] On config form POST: publish `MsgConfigUpdateRequest`
- [ ] On OTA file upload: forward raw chunks via `MsgOtaChunkReceived`
- [ ] Publish `MsgWebServerEvent` (STARTED, CONFIG_UPDATED)

---

## 11. ModuleTime  *(was `app_time` / timeman)*

- [ ] On init: try RTC → NTP → SPIFFS backup file (priority order)
- [ ] Subscribe to `MsgInternetStatus` (CONNECTED → sync NTP)
- [ ] Publish `MsgTimeStatus` (READY, UPDATED_FROM_RTC, UPDATED_FROM_NTP,
      UPDATED_FROM_BACKUP, UPDATE_FAILED_CRITICAL)
- [ ] Periodic: call `timeman_update_last_working_time()` → write epoch to SPIFFS
      via `MsgStorageWrite`

---

## 12. ModuleLeds  *(was `app_leds`)*

- [ ] Subscribe to `MsgWifiEvent`, `MsgInternetStatus`, `MsgNozzleState`,
      `MsgCloudStatus`, `MsgOtaEvent`
- [ ] Translate system state into LED patterns (idle, connected, pumping, OTA, error)
- [ ] Drive LED hardware via PAL; no outgoing messages needed

---

## 13. ModuleBuzzer  *(was `app_buzzer`)*

- [ ] Subscribe to `MsgNozzleState` (START → short beep, STOP → double beep)
- [ ] Subscribe to `MsgPrintBtnEvent` (button press confirm tone)
- [ ] Subscribe to `MsgOtaEvent` (success / failure tones)
- [ ] Drive buzzer GPIO via PAL; no outgoing messages needed

---

## 14. ModulePrintBtn  *(was `app_print_btn`)*

- [ ] Debounce two physical print buttons via `hsys_tog_button`
- [ ] Publish `MsgPrintBtnEvent` (PRINT1_SHORT, PRINT1_LONG, PRINT2_SHORT, PRINT2_LONG)
- [ ] No event-table callbacks; upstream modules subscribe to the message

---

## 15. ModuleDefaultBtn  *(was `app_default_btn`)*

- [ ] Debounce factory-reset / default button
- [ ] Publish `MsgDefaultBtnEvent` (SHORT_PRESS → log/info, LONG_PRESS → reset config)
- [ ] On long press: publish `MsgConfigResetRequest`

---

## 16. ModuleSpiffs  *(was `app_spiffs`)*

- [ ] Centralise all SPIFFS access; other modules must NOT call `pal_spiffs` directly
- [ ] Subscribe to `MsgStorageWrite` / `MsgStorageRead` / `MsgStorageAppend` /
      `MsgStorageDelete` requests
- [ ] Reply with `MsgStorageResult` (success / error + data for reads)
- [ ] Publish `MsgSpiffsReady` when mount succeeds at init

---

## 17. ModuleSd  *(was `app_sd`)*

- [ ] Same pattern as ModuleSpiffs but for SD card
- [ ] Subscribe to `MsgSdStorageRequest`; reply with `MsgSdStorageResult`
- [ ] Publish `MsgSdReady` when card is mounted

---

## 18. ModuleHw  *(was `app_hw`)*

- [ ] Hardware initialisation module (GPIO, ADC, UART setup that is not PAL-owned)
- [ ] Publish `MsgHwReady` once hardware is configured
- [ ] Subscribe to `MsgSystemReboot` to do a clean hardware shutdown before reset

---

## Cross-cutting tasks

- [ ] Remove `event_table_t` and `fp_event_interface_t` from `app_common.h` once all modules ported
- [ ] Remove global `_app_config`; replace with `ModuleConfig` messages
- [ ] Remove `hsys_taskrunner` vectors; tasks are created by `HsysTaskMgr`
- [ ] Update `app.cpp` to only call `app_init()` / `app_run()` (already the pattern)
- [ ] Audit every `while(1)` crash-on-null guard → replace with `log_error` + graceful degradation
