# TODO — Message Definitions for HSYS Architecture

Every message below must become a typed C++ class derived from `IHsysMsg`,
registered in `app_msg_ids.h` with a unique 16-bit ID, and implemented in
`src/app-messages/`.

Legend: `[ ]` = not started · `[~]` = in progress · `[x]` = done

---

## Actual ID space layout (from `app_msg_ids.h`)

```
0x0000           — reserved (HSYS_MSG_ID_INVALID)
0x0001 – 0x00FF  — sensor / demo data
0x0100 – 0x01FF  — timer service
0x0200 – 0x02FF  — system / lifecycle
0x0300 – 0x03FF  — config
0x0400 – 0x04FF  — WiFi
0x0500 – 0x05FF  — Internet
0x0600 – 0x06FF  — Cloud
0x0700 – 0x07FF  — MQTT
0x0800 – 0x08FF  — Fuel / Nozzle
0x0900 – 0x09FF  — Print / Printer
0x0A00 – 0x0AFF  — Buttons
0x0B00 – 0x0BFF  — OTA
0x0C00 – 0x0CFF  — Time / NTP
0x0D00 – 0x0DFF  — Storage (SPIFFS / SD)
0x0E00 – 0x0EFF  — Web Server
0x0F00 – 0x0FFF  — Retransmission
0xFFFF           — reserved (HSYS_MSG_ID_INVALID)
```

---

## Group 0x0001 — Sensor / Demo data

| ID     | Class           | Publisher | Subscribers |
|--------|-----------------|-----------|-------------|
| 0x0001 | `MsgSensorData` | ModuleA   | ModuleB     |

- [x] `MsgSensorData` — demo sensor reading (float value); will be removed in Sprint 12

---

## Group 0x0100 — Timer service

| ID     | Class                    | Publisher   | Subscribers          |
|--------|--------------------------|-------------|----------------------|
| 0x0100 | `MsgTimerStart`          | any module  | ModuleTimer          |
| 0x0101 | `MsgTimerStop`           | any module  | ModuleTimer          |
| 0x0102 | `MsgTimerStartResponse`  | ModuleTimer | requesting module    |
| 0x0103 | `MsgTimerStopResponse`   | ModuleTimer | requesting module    |
| 0x0104 | `MsgTimerAlarm`          | ModuleTimer | registered module    |

- [x] `MsgTimerStart` — payload: `source_module_id`, `start_offset_ms`, `duration_ms`, `is_repetitive`
- [x] `MsgTimerStop` — payload: `source_module_id`
- [x] `MsgTimerStartResponse` — DIRECT; payload: `source_module_id`, `result` (`timer_result_t`)
- [x] `MsgTimerStopResponse` — DIRECT; payload: `source_module_id`, `result` (`timer_result_t`)
- [x] `MsgTimerAlarm` — DIRECT; payload: `source_module_id`, `elapsed_ms`

---

## Group 0x0200 — System / Lifecycle

| ID     | Class              | Publisher        | Subscribers                               |
|--------|--------------------|------------------|-------------------------------------------|
| 0x0200 | `MsgTick1000ms`    | ModuleTicker     | ModuleSysmon, ModuleCloud (HB), ModuleRetransmit, ModuleOta |
| 0x0201 | `MsgSpiffsReady`   | ModuleSpiffs     | ModuleConfig, ModuleTime                  |
| 0x0202 | `MsgSystemReboot`  | ModuleOta, ModuleDefaultBtn | ModuleHw, all (graceful shutdown) |
| 0x0203 | `MsgHwReady`       | ModuleHw         | ModuleWifi, ModuleSpiffs, ModuleSd        |

- [x] `MsgTick1000ms` — no payload; 1 s heartbeat
- [x] `MsgSpiffsReady` — no payload; SPIFFS mounted and ready
- [ ] `MsgSystemReboot` — `reason` (enum: OTA_COMPLETE, CONFIG_RESET, WATCHDOG)
- [ ] `MsgHwReady` — no payload

---

## Group 0x0300 — Configuration

| ID     | Class                    | Publisher             | Subscribers                            |
|--------|--------------------------|-----------------------|----------------------------------------|
| 0x0300 | `MsgConfigReady`         | ModuleConfig          | ModuleWifi, ModuleCloud, ModuleMqtt, ModulePrinter |
| 0x0301 | `MsgConfigSet`           | ModuleWebServer, ModuleDefaultBtn | ModuleConfig           |
| 0x0302 | `MsgConfigGet`           | any module            | ModuleConfig                           |
| 0x0303 | `MsgConfigResetRequest`  | ModuleDefaultBtn      | ModuleConfig                           |

- [x] `MsgConfigReady` — published when config is loaded or updated; carries full `app_config_t` snapshot
- [x] `MsgConfigSet` — set one config field by key (`hsys_type_t` key + value bytes)
- [x] `MsgConfigGet` — no payload; triggers ModuleConfig to re-publish `MsgConfigReady`
- [ ] `MsgConfigResetRequest` — no payload; triggers factory-default reload

---

## Group 0x0400 — WiFi

| ID     | Class          | Publisher   | Subscribers                                     |
|--------|----------------|-------------|-------------------------------------------------|
| 0x0400 | `MsgWifiEvent` | ModuleWifi  | ModuleInternet, ModuleCloud, ModuleMqtt, ModuleWebServer, ModuleLeds |

- [ ] `MsgWifiEvent`
  ```
  app_wifi_event_t  event_id
  uint8_t           ip_address[4]   // valid on GOT_IP
  int8_t            rssi            // valid on RSSI_CHANGED
  ```
  Events: STA_START, STA_CONNECTED, STA_DISCONNECTED, STA_GOT_IP,
  AP_START, AP_STOP, AP_STA_CONNECTED, AP_STA_DISCONNECTED,
  STA_RSSI_CHANGED, NO_AVAILABLE_SIGNAL

---

## Group 0x0500 — Internet / Connectivity

| ID     | Class                | Publisher        | Subscribers                               |
|--------|----------------------|------------------|-------------------------------------------|
| 0x0500 | `MsgInternetStatus`  | ModuleInternet   | ModuleCloud, ModuleMqtt, ModuleOta, ModuleTime, ModuleLeds |

- [ ] `MsgInternetStatus`
  ```
  bool  connected
  ```

---

## Group 0x0600 — Cloud

| ID     | Class            | Publisher    | Subscribers                                |
|--------|------------------|--------------|--------------------------------------------|
| 0x0600 | `MsgCloudStatus` | ModuleCloud  | ModuleLeds, ModuleRetransmit, ModuleBuzzer |

- [ ] `MsgCloudStatus`
  ```
  app_cloud_event_t  event_id
  // CONFIG_READY, CONFIG_FAILED_RETRYING, PUMPED_SUCCESS, PUMPED_FAILED
  ```

---

## Group 0x0700 — MQTT

| ID     | Class                    | Publisher    | Subscribers                           |
|--------|--------------------------|--------------|---------------------------------------|
| 0x0700 | `MsgMqttEvent`           | ModuleMqtt   | ModuleLeds, ModuleOta                 |
| 0x0701 | `MsgMqttRxMessage`       | ModuleMqtt   | (topic-registered handlers)           |
| 0x0702 | `MsgMqttPublishRequest`  | any module   | ModuleMqtt                            |

- [ ] `MsgMqttEvent` — `pal_mqtt_event_t event_id` (CONNECTED, DISCONNECTED, SUBSCRIBED…)
- [ ] `MsgMqttRxMessage`
  ```
  char     topic[MAX_MQTT_TOPIC_LENGTH]
  uint8_t  payload[MAX_MQTT_PAYLOAD_LENGTH]
  uint16_t payload_len
  uint8_t  qos
  bool     dup
  bool     retain
  ```
- [ ] `MsgMqttPublishRequest`
  ```
  char     topic[MAX_MQTT_TOPIC_LENGTH]
  uint8_t  payload[MAX_MQTT_PAYLOAD_LENGTH]
  uint16_t payload_len
  uint8_t  qos
  bool     retain
  ```

---

## Group 0x0800 — Fuel / Nozzle

> **Design note — internal vs external events**
>
> The nozzle start/stop state machine and the Sanki display-tap decoder are
> **internal** to `ModuleFuel`.  Raw display-tap frames from the co-processor
> hardware need a message only because `ModuleDispTap` runs in a different task.
>
> Rule: a message is only created **when data must cross a task boundary**.
> Intra-module state (nozzle armed, state-machine intermediate values) stays as
> plain member variables.

| ID     | Class                  | Publisher       | Subscribers                                      |
|--------|------------------------|-----------------|--------------------------------------------------|
| 0x0800 | `MsgDispTapData`       | ModuleDispTap   | ModuleFuel                                       |
| 0x0801 | `MsgNozzleState`       | ModuleFuel      | ModuleLeds, ModuleBuzzer, ModuleCloud            |
| 0x0802 | `MsgFuelPumped`        | ModuleFuel      | ModuleCloud, ModulePrinter, ModuleRetransmit     |
| 0x0803 | `MsgDispTapFwVersion`  | ModuleDispTap   | ModuleOta                                        |

- [ ] `MsgDispTapData`
  ```
  uint8_t   nozzle_idx
  uint8_t   frame[DISPLAY_TAP_FRAME_MAX_SIZE]
  uint16_t  frame_len
  ```
- [ ] `MsgNozzleState` — published once at pump start and once at pump stop only
  ```
  uint8_t            nozzle_idx
  app_pump_event_t   state         // STARTED / STOPPED
  uint64_t           timestamp_ms
  ```
- [ ] `MsgFuelPumped` — published once when nozzle stop + valid data confirmed
  ```
  uint8_t   nozzle_idx
  uint8_t   fuel_type
  uint64_t  timestamp_ms
  uint32_t  unit_pricex100
  uint64_t  total_pricex100
  uint32_t  volume_lx1000
  ```
- [ ] `MsgDispTapFwVersion`
  ```
  uint8_t  nozzle_idx
  char     fw_version[16]
  ```

---

## Group 0x0900 — Print / Printer

| ID     | Class              | Publisher       | Subscribers                      |
|--------|--------------------|-----------------|----------------------------------|
| 0x0900 | `MsgPrintBtnEvent` | ModulePrintBtn  | ModulePrinter, ModuleBuzzer      |
| 0x0901 | `MsgPrintRequest`  | any module      | ModulePrinter                    |
| 0x0902 | `MsgPrinted`       | ModulePrinter   | ModuleCloud, ModuleRetransmit    |

- [ ] `MsgPrintBtnEvent` — `app_print_btn_event_t event_id` (PRINT1_SHORT, PRINT1_LONG, PRINT2_SHORT, PRINT2_LONG)
- [ ] `MsgPrintRequest`
  ```
  uint8_t   nozzle_idx
  char      nozzle_id[SIZE_OF_NOZZELID]
  char      fuel_type_str[SIZE_OF_FUEL_TYPE_STR]
  uint32_t  unit_pricex100
  uint64_t  total_pricex100
  uint32_t  volume_lx1000
  uint64_t  timestamp_ms
  uint8_t   copy_count
  ```
- [ ] `MsgPrinted` — `uint8_t nozzle_idx`, `bool success`, `int32_t error_code`

---

## Group 0x0A00 — Buttons (non-print)

| ID     | Class                  | Publisher           | Subscribers                       |
|--------|------------------------|---------------------|-----------------------------------|
| 0x0A00 | `MsgDefaultBtnEvent`   | ModuleDefaultBtn    | ModuleConfig, ModuleLeds          |

- [ ] `MsgDefaultBtnEvent` — `app_default_btn_event_t event_id` (SHORT_PRESS, LONG_PRESS)

---

## Group 0x0B00 — OTA

> **No OTA binary data crosses the message bus.** Only small control/status
> messages use the queue. Binary chunks are handled synchronously inside the
> PAL flash-writer or HTTP upload callback.
>
> Three OTA paths — all execute inside ModuleOta's task:
> - **Web-upload**: HTTP multipart POST → `ModuleWebServer` calls OTA driver directly
> - **Cloud-pull**: `MsgInternetStatus` CONNECTED + internal soft-timer
> - **MQTT-push**: `MsgOtaTrigger` wakes the cloud-pull path

| ID     | Class           | Publisher    | Subscribers                            |
|--------|-----------------|--------------|----------------------------------------|
| 0x0B00 | `MsgOtaEvent`   | ModuleOta    | ModuleLeds, ModuleBuzzer, ModuleCloud  |
| 0x0B01 | `MsgOtaTrigger` | ModuleMqtt   | ModuleOta                              |

- [ ] `MsgOtaEvent`
  ```
  app_ota_event_t  event_id      // CHECK_STARTED, CHECK_SUCCESS,
                                 // DOWNLOAD_STARTED, DOWNLOAD_SUCCESS,
                                 // DOWNLOAD_FAILURE
  uint8_t          driver_index  // 0 = esp32-main, 1 = esp07-coprocessor
  char             version[32]
  ```
- [ ] `MsgOtaTrigger`
  ```
  uint8_t  driver_index
  char     version[32]   // empty = "check latest"
  ```

---

## Group 0x0C00 — Time / NTP

| ID     | Class            | Publisher    | Subscribers                                  |
|--------|------------------|--------------|----------------------------------------------|
| 0x0C00 | `MsgTimeStatus`  | ModuleTime   | ModuleCloud, ModuleMqtt, ModuleLeds          |

- [ ] `MsgTimeStatus`
  ```
  app_timeman_event_t  event_id
  // READY, UPDATED_FROM_RTC, UPDATED_FROM_NTP, UPDATED_FROM_BACKUP, UPDATE_FAILED_CRITICAL
  time_t               epoch        // valid on any UPDATED_* event
  ```

---

## Group 0x0D00 — Storage (SPIFFS / SD)

| ID     | Class                | Publisher               | Subscribers             |
|--------|----------------------|-------------------------|-------------------------|
| 0x0D00 | `MsgStorageRequest`  | any module              | ModuleSpiffs / ModuleSd |
| 0x0D01 | `MsgStorageResult`   | ModuleSpiffs / ModuleSd | requesting module       |
| 0x0D02 | `MsgSdReady`         | ModuleSd                | ModuleRetransmit        |

- [ ] `MsgStorageRequest`
  ```
  uint8_t   op            // READ, WRITE, APPEND, DELETE, CREATE
  uint8_t   medium        // SPIFFS=0, SD=1
  char      path[64]
  uint8_t   data[256]     // for write / append
  uint16_t  data_len
  uint32_t  requester_id  // module ID to route reply back
  uint32_t  request_token // caller-assigned correlation ID
  ```
- [ ] `MsgStorageResult`
  ```
  uint32_t  request_token
  bool      success
  int32_t   error_code
  uint8_t   data[256]     // populated for READ
  uint16_t  data_len
  ```
- [ ] `MsgSdReady` — no payload

---

## Group 0x0E00 — Web Server

| ID     | Class                | Publisher         | Subscribers              |
|--------|----------------------|-------------------|--------------------------|
| 0x0E00 | `MsgWebServerEvent`  | ModuleWebServer   | ModuleLeds               |

- [ ] `MsgWebServerEvent` — `app_webserver_event_t event_id` (STARTED, CONFIG_UPDATED)

---

## Group 0x0F00 — Retransmission

| ID     | Class                  | Publisher           | Subscribers             |
|--------|------------------------|---------------------|-------------------------|
| 0x0F00 | `MsgRetransmitStatus`  | ModuleRetransmit    | ModuleLeds              |

- [ ] `MsgRetransmitStatus`
  ```
  uint32_t  pending_count
  bool      last_retry_ok
  int32_t   last_error_code
  ```

---

## Summary — IDs at a glance

```
0x0001  MsgSensorData            [x]  (demo — remove Sprint 12)

0x0100  MsgTimerStart            [x]
0x0101  MsgTimerStop             [x]
0x0102  MsgTimerStartResponse    [x]
0x0103  MsgTimerStopResponse     [x]
0x0104  MsgTimerAlarm            [x]

0x0200  MsgTick1000ms            [x]
0x0201  MsgSpiffsReady           [x]
0x0202  MsgSystemReboot          [ ]
0x0203  MsgHwReady               [ ]

0x0300  MsgConfigReady           [x]
0x0301  MsgConfigSet             [x]
0x0302  MsgConfigGet             [x]
0x0303  MsgConfigResetRequest    [ ]

0x0400  MsgWifiEvent             [ ]

0x0500  MsgInternetStatus        [ ]

0x0600  MsgCloudStatus           [ ]

0x0700  MsgMqttEvent             [ ]
0x0701  MsgMqttRxMessage         [ ]
0x0702  MsgMqttPublishRequest    [ ]

0x0800  MsgDispTapData           [ ]
0x0801  MsgNozzleState           [ ]
0x0802  MsgFuelPumped            [ ]
0x0803  MsgDispTapFwVersion      [ ]

0x0900  MsgPrintBtnEvent         [ ]
0x0901  MsgPrintRequest          [ ]
0x0902  MsgPrinted               [ ]

0x0A00  MsgDefaultBtnEvent       [ ]

0x0B00  MsgOtaEvent              [ ]
0x0B01  MsgOtaTrigger            [ ]

0x0C00  MsgTimeStatus            [ ]

0x0D00  MsgStorageRequest        [ ]
0x0D01  MsgStorageResult         [ ]
0x0D02  MsgSdReady               [ ]

0x0E00  MsgWebServerEvent        [ ]

0x0F00  MsgRetransmitStatus      [ ]
```

| 0x0102 | `MsgSystemReboot`        | ModuleOta, ModuleDefaultBtn | ModuleHw, all (graceful shutdown) |
| 0x0103 | `MsgHwReady`             | ModuleHw         | ModuleWifi, ModuleSpiffs, ModuleSd       |

- [x] `MsgTick1000ms` — already implemented
- [ ] `MsgSystemReboot` — `reason` (enum: OTA_COMPLETE, CONFIG_RESET, WATCHDOG)
- [ ] `MsgHwReady` — no payload

---

## Group 0x02xx — Configuration

| ID     | Class                      | Publisher             | Subscribers                            |
|--------|----------------------------|-----------------------|----------------------------------------|
| 0x0201 | `MsgConfigLoaded`          | ModuleConfig          | ModuleWifi, ModuleCloud, ModuleMqtt, ModulePrinter |
| 0x0202 | `MsgConfigUpdateRequest`   | ModuleWebServer, ModuleDefaultBtn | ModuleConfig           |
| 0x0203 | `MsgConfigResetRequest`    | ModuleDefaultBtn      | ModuleConfig                           |

- [ ] `MsgConfigLoaded` — carries full `app_config_t` snapshot (wifi ssid/pw, cloud url/secret, mqtt broker/port, printer url, nozzle configs, logging flags)
- [ ] `MsgConfigUpdateRequest` — `key` (string), `value` (byte buffer + type), `timeout_ms`
- [ ] `MsgConfigResetRequest` — no payload; triggers factory-default reload

---

## Group 0x03xx — WiFi

| ID     | Class                | Publisher     | Subscribers                                     |
|--------|----------------------|---------------|-------------------------------------------------|
| 0x0301 | `MsgWifiEvent`       | ModuleWifi    | ModuleInternet, ModuleCloud, ModuleMqtt, ModuleWebServer, ModuleLeds |

- [ ] `MsgWifiEvent`
  ```
  app_wifi_event_t  event_id
  uint8_t           ip_address[4]   // valid on GOT_IP
  int8_t            rssi            // valid on RSSI_CHANGED
  ```
  Events: STA_START, STA_CONNECTED, STA_DISCONNECTED, STA_GOT_IP,
  AP_START, AP_STOP, AP_STA_CONNECTED, AP_STA_DISCONNECTED,
  STA_RSSI_CHANGED, NO_AVAILABLE_SIGNAL

---

## Group 0x04xx — Internet / Connectivity

| ID     | Class                  | Publisher        | Subscribers                               |
|--------|------------------------|------------------|-------------------------------------------|
| 0x0401 | `MsgInternetStatus`    | ModuleInternet   | ModuleCloud, ModuleMqtt, ModuleOta, ModuleTime, ModuleLeds |

- [ ] `MsgInternetStatus`
  ```
  bool    connected
  ```

---

## Group 0x05xx — Cloud

| ID     | Class                | Publisher      | Subscribers                                |
|--------|----------------------|----------------|--------------------------------------------|
| 0x0501 | `MsgCloudStatus`     | ModuleCloud    | ModuleLeds, ModuleRetransmit, ModuleBuzzer |

- [ ] `MsgCloudStatus`
  ```
  app_cloud_event_t   event_id
  // CONFIG_READY, CONFIG_FAILED_RETRYING, PUMPED_SUCCESS, PUMPED_FAILED
  ```

---

## Group 0x06xx — MQTT

| ID     | Class                    | Publisher        | Subscribers                           |
|--------|--------------------------|------------------|---------------------------------------|
| 0x0601 | `MsgMqttEvent`           | ModuleMqtt       | ModuleLeds, ModuleOta                 |
| 0x0602 | `MsgMqttRxMessage`       | ModuleMqtt       | (topic-registered handlers)           |
| 0x0603 | `MsgMqttPublishRequest`  | any module       | ModuleMqtt                            |

- [ ] `MsgMqttEvent`
  ```
  pal_mqtt_event_t  event_id   // CONNECTED, DISCONNECTED, SUBSCRIBED…
  ```
- [ ] `MsgMqttRxMessage`
  ```
  char     topic[MAX_MQTT_TOPIC_LENGTH]
  uint8_t  payload[MAX_MQTT_PAYLOAD_LENGTH]
  uint16_t payload_len
  uint8_t  qos
  bool     dup
  bool     retain
  ```
- [ ] `MsgMqttPublishRequest`
  ```
  char     topic[MAX_MQTT_TOPIC_LENGTH]
  uint8_t  payload[MAX_MQTT_PAYLOAD_LENGTH]
  uint16_t payload_len
  uint8_t  qos
  bool     retain
  ```

---

## Group 0x07xx — Fuel / Nozzle

> **Design note — internal vs external events**
>
> The nozzle start/stop state machine and the Sanki display-tap decoder are
> **internal** to `ModuleFuel`.  The old app exposed per-nozzle callbacks
> (`on_nozzle1_start`, `on_nozzle2_start`, etc.) only so the flat task runner
> could call them from outside.  In the new architecture `ModuleFuel` owns the
> `hsys_tog_button` debouncers and the Sanki state machine directly — they run
> inside the module's own task and do **not** require messages.  Raw display-tap
> frames from the co-processor hardware do need a message because `ModuleDispTap`
> runs in a different task.
>
> Rule: a message is only created **when data must cross a task boundary**.
> Intra-module state (nozzle armed, state-machine intermediate values) stays as
> plain member variables.

| ID     | Class               | Publisher       | Subscribers                                      |
|--------|---------------------|-----------------|--------------------------------------------------|
| 0x0701 | `MsgDispTapData`    | ModuleDispTap   | ModuleFuel                                       |
| 0x0702 | `MsgNozzleState`    | ModuleFuel      | ModuleLeds, ModuleBuzzer, ModuleCloud            |
| 0x0703 | `MsgFuelPumped`     | ModuleFuel      | ModuleCloud, ModulePrinter, ModuleRetransmit     |
| 0x0704 | `MsgDispTapFwVersion` | ModuleDispTap | ModuleOta (triggers esp07 OTA driver ready)      |

- [ ] `MsgDispTapData`
  ```
  uint8_t   nozzle_idx
  uint8_t   frame[DISPLAY_TAP_FRAME_MAX_SIZE]
  uint16_t  frame_len
  ```
- [ ] `MsgNozzleState`
  ```
  uint8_t            nozzle_idx
  app_pump_event_t   state         // STARTED / STOPPED
  uint64_t           timestamp_ms
  ```
  > Published **once** at pump start and once at pump stop only — not for every
  > display-tap frame. `ModuleFuel` decides the transition internally.
- [ ] `MsgFuelPumped`  *(derived from `nozzle_event_t`)*
  ```
  uint8_t   nozzle_idx
  uint8_t   fuel_type
  uint64_t  timestamp_ms
  uint32_t  unit_pricex100
  uint64_t  total_pricex100
  uint32_t  volume_lx1000
  ```
  > Published **once** when the nozzle stop + valid data is confirmed. The
  > entire nozzle transaction accumulation (unit price, volume, total) happens
  > inside `ModuleFuel` with no intermediate messages.
- [ ] `MsgDispTapFwVersion`
  ```
  uint8_t  nozzle_idx
  char     fw_version[16]
  ```

---

## Group 0x08xx — Print / Printer

| ID     | Class                | Publisher           | Subscribers                      |
|--------|----------------------|---------------------|----------------------------------|
| 0x0801 | `MsgPrintBtnEvent`   | ModulePrintBtn      | ModulePrinter, ModuleBuzzer      |
| 0x0802 | `MsgPrintRequest`    | any module          | ModulePrinter                    |
| 0x0803 | `MsgPrinted`         | ModulePrinter       | ModuleCloud, ModuleRetransmit    |

- [ ] `MsgPrintBtnEvent`
  ```
  app_print_btn_event_t  event_id
  // PRINT1_SHORT, PRINT1_LONG, PRINT2_SHORT, PRINT2_LONG
  ```
- [ ] `MsgPrintRequest`
  ```
  uint8_t   nozzle_idx
  char      nozzle_id[SIZE_OF_NOZZELID]
  char      fuel_type_str[SIZE_OF_FUEL_TYPE_STR]
  uint32_t  unit_pricex100
  uint64_t  total_pricex100
  uint32_t  volume_lx1000
  uint64_t  timestamp_ms
  uint8_t   copy_count
  ```
- [ ] `MsgPrinted`
  ```
  uint8_t  nozzle_idx
  bool     success
  int32_t  error_code
  ```

---

## Group 0x09xx — Buttons (non-print)

| ID     | Class                  | Publisher           | Subscribers                       |
|--------|------------------------|---------------------|-----------------------------------|
| 0x0901 | `MsgDefaultBtnEvent`   | ModuleDefaultBtn    | ModuleConfig, ModuleLeds          |

- [ ] `MsgDefaultBtnEvent`
  ```
  app_default_btn_event_t  event_id   // SHORT_PRESS, LONG_PRESS
  ```

---

## Group 0x0Axx — OTA

> **Design note — OTA and the message bus**
>
> OTA has three paths, all of which run in `ModuleOta`'s own task:
>
> | Path | Trigger | Binary source | Target |
> |---|---|---|---|
> | **Web-upload** | HTTP multipart POST to `/updateFirmwareBin` or `/updateEsp07FirmwareBin` | LAN browser | ESP32-main or ESP07 co-proc |
> | **Cloud-pull** | `MsgInternetStatus` CONNECTED → periodic HTTP check to OTA server | Cloud HTTP | ESP32-main or ESP07 co-proc |
> | **MQTT-push** | `MsgMqttRxMessage` on OTA-trigger topic | MQTT broker | ESP32-main or ESP07 co-proc |
>
> ### Why OTA must stay off the message bus for data
>
> Firmware images can be up to ~1 MB.  Routing binary chunks through the HSYS
> message pool would require either giant pool blocks (wasting RAM) or a
> fragmentation/reassembly layer (adding latency and complexity).  More
> importantly, the ESP-IDF `pal_fw_update_write()` call needs to stream data
> synchronously into the OTA partition — any queue hop between the HTTP/MQTT
> receive callback and the flash writer adds avoidable latency and an extra
> copy.
>
> **Rule: OTA binary data never enters the message pool.**
>
> - **Web-upload path**: `ModuleWebServer` receives the HTTP multipart upload
>   entirely in the HTTP server task and calls `ModuleOta`'s write function
>   directly via a registered `hsys_ota_driver_t*` pointer (same as old
>   `aws_cb_table`). No message needed — both live in the same process space and
>   the HTTP handler is already synchronous.
> - **Cloud-pull path**: `ModuleOta` runs the download-and-flash loop
>   entirely inside its own task, driven by an internal soft-timer and
>   `MsgInternetStatus` as the wake signal.
> - **MQTT-push path**: `MsgMqttRxMessage` (a small JSON control message, not
>   the binary) wakes `ModuleOta` to start a cloud-pull.  The binary still
>   flows through the cloud-pull path.
>
> ### What messages ARE used for OTA
>
> Only small control/status messages cross the bus:

| ID     | Class                  | Publisher         | Subscribers                            |
|--------|------------------------|-------------------|----------------------------------------|
| 0x0A01 | `MsgOtaEvent`          | ModuleOta         | ModuleLeds, ModuleBuzzer, ModuleCloud  |
| 0x0A02 | `MsgOtaTrigger`        | ModuleMqtt        | ModuleOta                              |

- [ ] `MsgOtaEvent`
  ```
  app_ota_event_t  event_id      // CHECK_STARTED, CHECK_SUCCESS,
                                 // DOWNLOAD_STARTED, DOWNLOAD_SUCCESS,
                                 // DOWNLOAD_FAILURE
  uint8_t          driver_index  // 0 = esp32-main, 1 = esp07-coprocessor
  char             version[32]   // new version string (on DOWNLOAD_SUCCESS)
  ```
  > On DOWNLOAD_SUCCESS `ModuleOta` publishes this, then calls `pal_power_reset()`
  > after a short delay to let subscribers act (e.g. LEDs flash confirmation).

- [ ] `MsgOtaTrigger`
  ```
  uint8_t  driver_index    // which driver to trigger
  char     version[32]     // optional: specific version, empty = "check latest"
  ```
  > Published by `ModuleMqtt` when the MQTT broker sends an OTA-trigger
  > control message. `ModuleOta` treats it identically to a timer-fired check.

> **No `MsgOtaChunkReceived`** — web-upload binary chunks are passed directly
> via the `hsys_ota_driver_t` callback pointer registered at init, never
> through the queue.

---

## Group 0x0Bxx — Time / NTP

| ID     | Class              | Publisher      | Subscribers                                  |
|--------|--------------------|----------------|----------------------------------------------|
| 0x0B01 | `MsgTimeStatus`    | ModuleTime     | ModuleCloud, ModuleMqtt, ModuleLeds          |

- [ ] `MsgTimeStatus`
  ```
  app_timeman_event_t  event_id
  // READY, UPDATED_FROM_RTC, UPDATED_FROM_NTP, UPDATED_FROM_BACKUP, UPDATE_FAILED_CRITICAL
  time_t               epoch        // valid on any UPDATED_* event
  ```

---

## Group 0x0Cxx — Storage (SPIFFS / SD)

| ID     | Class                   | Publisher              | Subscribers             |
|--------|-------------------------|------------------------|-------------------------|
| 0x0C01 | `MsgStorageRequest`     | any module             | ModuleSpiffs / ModuleSd |
| 0x0C02 | `MsgStorageResult`      | ModuleSpiffs / ModuleSd | requesting module      |
| 0x0C03 | `MsgSpiffsReady`        | ModuleSpiffs           | ModuleConfig, ModuleTime|
| 0x0C04 | `MsgSdReady`            | ModuleSd               | ModuleRetransmit        |

- [ ] `MsgStorageRequest`
  ```
  uint8_t   op            // READ, WRITE, APPEND, DELETE, CREATE
  uint8_t   medium        // SPIFFS=0, SD=1
  char      path[64]
  uint8_t   data[256]     // for write / append
  uint16_t  data_len
  uint32_t  requester_id  // module ID to route reply back
  uint32_t  request_token // caller-assigned correlation ID
  ```
- [ ] `MsgStorageResult`
  ```
  uint32_t  request_token
  bool      success
  int32_t   error_code
  uint8_t   data[256]     // populated for READ
  uint16_t  data_len
  ```
- [ ] `MsgSpiffsReady` — no payload
- [ ] `MsgSdReady`     — no payload

---

## Group 0x0Dxx — Web Server

| ID     | Class                  | Publisher         | Subscribers              |
|--------|------------------------|-------------------|--------------------------|
| 0x0D01 | `MsgWebServerEvent`    | ModuleWebServer   | ModuleLeds               |

- [ ] `MsgWebServerEvent`
  ```
  app_webserver_event_t  event_id   // STARTED, CONFIG_UPDATED
  ```

---

## Group 0x0Exx — Retransmission

| ID     | Class                    | Publisher           | Subscribers             |
|--------|--------------------------|---------------------|-------------------------|
| 0x0E01 | `MsgRetransmitStatus`    | ModuleRetransmit    | ModuleLeds              |

- [ ] `MsgRetransmitStatus`
  ```
  uint32_t  pending_count
  bool      last_retry_ok
  int32_t   last_error_code
  ```

---

## Summary — IDs at a glance

```
0x0101  MsgTick1000ms            [x]
0x0102  MsgSystemReboot          [ ]
0x0103  MsgHwReady               [ ]
0x0201  MsgConfigLoaded          [ ]
0x0202  MsgConfigUpdateRequest   [ ]
0x0203  MsgConfigResetRequest    [ ]
0x0301  MsgWifiEvent             [ ]
0x0401  MsgInternetStatus        [ ]
0x0501  MsgCloudStatus           [ ]
0x0601  MsgMqttEvent             [ ]
0x0602  MsgMqttRxMessage         [ ]
0x0603  MsgMqttPublishRequest    [ ]
0x0701  MsgDispTapData           [ ]
0x0702  MsgNozzleState           [ ]
0x0703  MsgFuelPumped            [ ]
0x0704  MsgDispTapFwVersion      [ ]
0x0801  MsgPrintBtnEvent         [ ]
0x0802  MsgPrintRequest          [ ]
0x0803  MsgPrinted               [ ]
0x0901  MsgDefaultBtnEvent       [ ]
0x0A01  MsgOtaEvent              [ ]
0x0A02  MsgOtaTrigger            [ ]   ← MQTT-push control only; no binary chunks
0x0B01  MsgTimeStatus            [ ]
0x0C01  MsgStorageRequest        [ ]
0x0C02  MsgStorageResult         [ ]
0x0C03  MsgSpiffsReady           [ ]
0x0C04  MsgSdReady               [ ]
0x0D01  MsgWebServerEvent        [ ]
0x0E01  MsgRetransmitStatus      [ ]
```
