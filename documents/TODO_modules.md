# TODO — Module Port to HSYS Message Queue Architecture

Each old component used a flat `event_table_t` of raw function pointers and passed
data as `void *` arg. The new architecture gives every component its own
`HsysModule` subclass living in a dedicated FreeRTOS task, communicating only
through typed, ref-counted messages on the queue.

Legend: `[ ]` = not started · `[~]` = in progress · `[x]` = done

---

## Progress Summary *(updated 2026-04-24)*

| # | Module | Status | Notes |
|---|---|---|---|
| 1 | ModuleConfig | ✅ done | 257 lines, 0 TODOs |
| 2 | ModuleWifi | ✅ done | 216 lines, 0 TODOs |
| 3 | ModuleInternet | ✅ done | 190 lines, 0 TODOs |
| 4 | ModuleCloud | 🔶 in progress | 379 lines, 4 TODOs (retransmit, timestamp, version, driver wiring) |
| 5 | ModuleMqtt | ❌ not started | — |
| 6 | ModuleFuel | ✅ done | 446 lines, 0 TODOs — display-tap logic integrated internally |
| 7 | ModuleRetransmit | ❌ not started | Referenced by ModuleCloud (TODOs at lines 181, 208) |
| 8 | ModuleOta | ❌ not started | — |
| 9 | ModuleWebServer | ❌ not started | — |
| 10 | ModuleTimeMgr | ✅ done | 347 lines, 0 TODOs (implemented as `module_timemgr`) |
| 11 | ModuleLeds | 🔶 partial | 82 lines; only subscribes to `MsgSpiffsReady` — missing WiFi/cloud/OTA subscriptions |
| 12 | ModuleBuzzer | 🔶 partial | 113 lines; subscribes to `MsgPrinterBtn` + `MsgFuelPumped` — missing OTA events |
| 13 | ModulePrintBtn | ✅ done | 136 lines, 0 TODOs |
| 14 | ModuleDefaultBtn | ✅ done | 92 lines, 0 TODOs |
| 15 | ModuleSpiffs | 🔶 partial | 54 lines; mounts and publishes `MsgSpiffsReady` — no `MsgStorageWrite/Read` broker yet |
| 16 | ModuleSD | 🔶 partial | 86 lines; mounts and publishes `MsgSdReady/Status` — no `MsgSdStorageRequest` broker yet |
| 17 | ModuleHw | ❌ not started | GPIO/ADC/UART init that is not PAL-owned |

### Known gaps (pre-existing, not blocking build)
- `app_msg_table.h`: `MsgTimeStatus`, `MsgWifiEvent`, `MsgInternetStatus`, `MsgCloudStatus` are **defined and compiled** but **missing from the descriptor table** — runtime pool allocations for these messages will fail silently
- `user_config.h`: log-enable flags missing for most modules and PAL implementations
- `ModuleCloud::set_driver()` is never called from `main.cpp` — cloud driver is not wired
- Firmware version hardcoded `"1.0.0"` in `module_cloud.cpp` (no `version.h` exists yet)

---

## 1. ModuleConfig  *(was `app_config`)*

- [x] Port `app_config_t` (wifi, cloud, mqtt, device-type) to a config-store module
- [x] On init: subscribe to `MsgSpiffsReady`, load config from SPIFFS JSON, publish `MsgConfigReady`
- [x] On `MsgConfigSet`: save new value, re-publish `MsgConfigReady`
- [x] On `MsgConfigGet`: re-publish `MsgConfigReady`
- [x] On typed domain requests (`MsgConfigGetWifi/Cloud/Mqtt/DT`): reply DIRECT with `MsgConfigWifi/Cloud/Mqtt/DT`
- [x] No more global `_app_config`; config is owned by this module

---

## 2. ModuleWifi  *(was `app_wifi`)*

- [x] Wrap PAL wifi driver; subscribe to `MsgConfigReady` → request `MsgConfigGetWifi` → receive `MsgConfigWifi`
- [x] Publish `MsgWifiEvent` for every PAL wifi state change (STA_CONNECTED, STA_DISCONNECTED, STA_GOT_IP)
- [x] Internal soft-timer (via `MsgTimerStart`) for reconnect back-off
- [x] Replace `_on_config_event` callback with subscription to `MsgConfigReady`

---

## 3. ModuleInternet  *(was `app_internet`)*

- [x] Subscribe to `MsgWifiEvent` (GOT_IP → start periodic ping; DISCONNECTED → report offline)
- [x] Perform periodic ICMP/HTTP reachability check using PAL ping
- [x] Publish `MsgInternetStatus` (CONNECTED / DISCONNECTED) on change only
- [ ] Consumers: ModuleCloud ✅ · ModuleMqtt ❌ · ModuleOta ❌

---

## 4. ModuleCloud  *(was `app_cloud`)*

- [x] Subscribe to `MsgConfigReady` → request cloud config via `MsgConfigGetCloud`
- [x] Subscribe to `MsgWifiEvent`, `MsgInternetStatus`, `MsgFuelPumped`, `MsgTimerAlarm`, `MsgTick1000ms`
- [x] On internet connected: send device registration, then periodic heartbeat
- [x] On `MsgFuelPumped`: call cloud driver `fp_on_cloud_event_pumped_rqst`
- [x] Publish `MsgCloudStatus` (REGISTERED, REGISTER_FAILED, PUMPED_SUCCESS, PUMPED_FAILED, HB_SENT, HB_FAILED)
- [x] `cloud_driver_t` injected via `set_driver()` (dependency injection)
- [ ] **OPEN**: `set_driver()` never called from `main.cpp` — driver must be wired before `app_init()`
- [ ] **OPEN**: `info.time_stamp = 0` (line 189) — populate from `pal_time_get_unix()` once `MsgTimeStatus` is subscribed
- [ ] **OPEN**: `fw_version` hardcoded `"1.0.0"` (line 351) — replace with `version.h` constant
- [ ] **OPEN**: publish `MsgRetransmitStore` on PUMPED_FAILED (lines 181, 208) — requires `ModuleRetransmit`

---

## 5. ModuleMqtt  *(was `app_mqtt`)*

- [ ] Subscribe to `MsgWifiEvent` (GOT_IP → connect broker), `MsgInternetStatus`
- [ ] Subscribe to `MsgConfigReady` → request `MsgConfigGetMqtt` for broker URI / port / topic table
- [ ] Publish `MsgMqttEvent` (CONNECTED, DISCONNECTED, MSG_RECEIVED)
- [ ] Carry inbound payload in `MsgMqttRxMessage` (topic + payload buffer)
- [ ] Provide `app_mqtt_publish()` equivalent via `MsgMqttPublishRequest`
- [ ] Topic-to-handler table stays; handlers become message publishers

---

## 6. ModuleFuel  *(was `app_fuel` + `app_disptap`)*

> Core domain module — nozzle start/stop capture and Sanki 6-digit state machine.
>
> `ModuleFuel` **owns** both the `hsys_tog_button` debouncers and the Sanki
> state machine.  They execute inside `ModuleFuel`'s task and communicate only
> through private member variables — **no messages for intra-module state**.
> A message is only sent when a **completed transaction result** must be
> delivered to other tasks (cloud, printer, retransmit, LEDs, buzzer).

- [x] Subscribe to `MsgConfigReady` → request `MsgConfigGetDT` for display-type / HW settings
- [x] Run Sanki 6-digit state machine **internally** on each hardware poll cycle
- [x] Drive `hsys_tog_button` debouncers **internally** — no nozzle-start/stop messages to self
- [x] On nozzle stop + valid complete data: publish `MsgFuelPumped` (one per transaction)
- [x] On nozzle start/stop state change: publish `MsgNozzleState` (for LEDs/buzzer/cloud status)
- [x] All intermediate display-tap values (unit price, running volume, total) are plain member variables
---

## 7. ModuleRetransmit  *(was `app_retransmit` + `retransmission_manager`)*

> **Next module to implement.** ModuleCloud already has the publish calls stubbed
> at lines 181 and 208 of `module_cloud.cpp`.

- [ ] Subscribe to `MsgFuelPumped` to detect transactions that need cloud ACK
- [ ] Subscribe to `MsgCloudStatus` to know when cloud is reachable for retry
- [ ] On cloud PUMPED_FAILED: persist serialised event to SD/SPIFFS
- [ ] Periodic retry: re-publish `MsgFuelPumped` (or a dedicated `MsgRetransmitRetry`)
- [ ] Publish `MsgRetransmitStatus` (pending count, last retry result)
- [ ] New message IDs needed: `MSG_ID_RETRANSMIT_STORE` (0x0B00), `MSG_ID_RETRANSMIT_STATUS` (0x0B01)

---

## 8. ModuleOta  *(was `app_ota`)*

> ### Three OTA paths — all execute inside ModuleOta's task
>
> | Path | Wake source | How binary arrives |
> |---|---|---|
> | **Web-upload** | HTTP upload callback (synchronous) | `ModuleWebServer` calls `hsys_ota_driver_t::fw_drv->fp_write()` directly |
> | **Cloud-pull** | `MsgInternetStatus` CONNECTED + internal soft-timer (300 s) | ModuleOta downloads via `fp_download_and_flash()` |
> | **MQTT-push** | `MsgOtaTrigger` | Same cloud-pull path, triggered immediately |

- [ ] Maintain array of `hsys_ota_driver_t*` (one per firmware target: esp32-main, esp07-coprocessor)
- [ ] Each driver has its own state machine: WAIT_INTERNET → CHECKING → DOWNLOAD_PENDING → DOWNLOADING
- [ ] Subscribe to `MsgInternetStatus` (CONNECTED → start 300 s timer + immediate check)
- [ ] Subscribe to `MsgOtaTrigger` (MQTT-push path)
- [ ] Subscribe to `MsgDispTapFwVersion` (marks esp07 driver ready)
- [ ] Subscribe to `MsgSpiffsReady` (marks esp32-main driver ready)
- [ ] Publish `MsgOtaEvent` on state transitions (CHECK_STARTED, DOWNLOAD_STARTED, DOWNLOAD_SUCCESS, DOWNLOAD_FAILURE)
- [ ] On DOWNLOAD_SUCCESS: publish `MsgOtaEvent`, wait ~500 ms, call `pal_power_reset()`

---

## 9. ModuleWebServer  *(was `app_webserver`)*

- [ ] Subscribe to `MsgWifiEvent` (AP_START → start HTTP server; AP_STOP → stop)
- [ ] On config form POST: publish `MsgConfigSet`
- [ ] On OTA file upload: call OTA driver write callbacks directly (no queue)
- [ ] Publish `MsgWebServerEvent` (STARTED, CONFIG_UPDATED)

---

## 10. ModuleTimeMgr  *(was `app_time` / timeman — implemented as `module_timemgr`)*

- [x] On init: arm SPIFFS-wait timer; fall back to RTC if SPIFFS times out
- [x] Subscribe to `MsgSpiffsReady` → load SPIFFS backup epoch, publish `MsgTimeStatus`
- [x] Subscribe to `MsgInternetStatus` (CONNECTED → start NTP sync via `pal_ntp`)
- [x] Poll `pal_ntp_timesync_process()` on `MsgTimerAlarm` during NTP sync
- [x] In READY: 5-minute backup timer writes `timemgr.bin` to SPIFFS
- [x] Publish `MsgTimeStatus` (epoch, source, valid flag) on every source change
- [ ] **OPEN**: `MsgTimeStatus::DESCRIPTOR` missing from `app_msg_table.h` — add entry

---

## 11. ModuleLeds  *(was `app_leds`)*

- [x] Drive LED hardware via PAL
- [x] Subscribe to `MsgSpiffsReady` (storage ready indication)
- [ ] Subscribe to `MsgWifiEvent` (connected / disconnected LED pattern)
- [ ] Subscribe to `MsgInternetStatus`
- [ ] Subscribe to `MsgNozzleState` (pumping LED)
- [ ] Subscribe to `MsgCloudStatus`
- [ ] Subscribe to `MsgOtaEvent` (OTA in-progress LED)

---

## 12. ModuleBuzzer  *(was `app_buzzer`)*

- [x] Drive buzzer GPIO via PAL
- [x] Subscribe to `MsgPrinterBtn` (button press confirm tone)
- [x] Subscribe to `MsgFuelPumped` (transaction complete tone)
- [x] Subscribe to `MsgConfigReady` (config-loaded tone)
- [ ] Subscribe to `MsgNozzleState` (nozzle start/stop tones)
- [ ] Subscribe to `MsgOtaEvent` (success / failure tones)

---

## 13. ModulePrintBtn  *(was `app_print_btn`)*

- [x] Debounce two physical print buttons via `hsys_tog_button`
- [x] Publish `MsgPrinterBtn` (PRINT1_SHORT, PRINT1_LONG, PRINT2_SHORT, PRINT2_LONG)
- [x] No event-table callbacks; upstream modules subscribe to the message

---

## 14. ModuleDefaultBtn  *(was `app_default_btn`)*

- [x] Debounce factory-reset / default button via `hsys_tog_button`
- [x] Publish `MsgDefaultBtn` (SHORT_PRESS, LONG_PRESS)
- [ ] On long press: publish `MsgConfigSet` to trigger config reset (currently only logs)

---

## 15. ModuleSpiffs  *(was `app_spiffs`)*

- [x] Mount SPIFFS on init
- [x] Publish `MsgSpiffsReady` when mount succeeds
- [ ] Centralise all SPIFFS access: subscribe to `MsgStorageWrite` / `MsgStorageRead` / `MsgStorageDelete`
- [ ] Reply with `MsgStorageResult` (success / error + data for reads)
- [ ] Other modules currently call `pal_spiffs` directly — migrate once broker pattern is needed

---

## 16. ModuleSD  *(was `app_sd`)*

- [x] Mount SD card on init
- [x] Publish `MsgSdReady` and `MsgSdStatus` (type, size, free space)
- [ ] Subscribe to `MsgSdStorageRequest`; reply with `MsgSdStorageResult`
- [ ] Centralise SD access (currently other modules call PAL directly)

---

## 17. ModuleHw  *(was `app_hw`)*

- [ ] Hardware initialisation module (GPIO, ADC, UART setup that is not PAL-owned)
- [ ] Publish `MsgHwReady` once hardware is configured
- [ ] Subscribe to `MsgSystemReboot` to do a clean hardware shutdown before reset

---

## Cross-cutting tasks

- [x] Remove `hsys_taskrunner` vectors; tasks are created by `HsysTaskMgr`
- [x] `app.cpp` calls only `app_init()` / `app_run()`
- [ ] Add missing descriptors to `app_msg_table.h`: `MsgTimeStatus`, `MsgWifiEvent`, `MsgInternetStatus`, `MsgCloudStatus`
- [ ] Add log-enable flags in `user_config.h` for all modules and PAL implementations
- [ ] Wire `ModuleCloud::set_driver(cloud_driver_cube_sphere())` in `main.cpp` before `app_init()`
- [ ] Create `src/product/app/version.h` with `FW_VERSION_STRING` constant
- [ ] Remove `event_table_t` and `fp_event_interface_t` from `app_common.h` once all modules ported
- [ ] Audit every `while(1)` crash-on-null guard → replace with `log_error` + graceful degradation
