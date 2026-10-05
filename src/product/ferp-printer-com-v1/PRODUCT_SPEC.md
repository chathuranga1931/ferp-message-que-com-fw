# FERP receipt printer — Product Specification

Covers both printer products. They share everything except the link to the printer:

| Product | Folder | Chip | Printer link | Version | OTA target |
|---|---|---|---|---|---|
| COM printer | `ferp-printer-com-v1/` | ESP32 (board 2308) | UART2 (TX 17 / RX 16) | 44.x | `printer-com-main` |
| USB printer | `ferp-printer-usb-v1/` | ESP32-S3 | USB host, bulk OUT endpoint | 45.x | `printer-usb-main` |

They replace the legacy Arduino firmware (`old/ferp-printer`, 42.x) and keep its
partition table, HTTP API, config file and bill counter. A legacy printer is
upgraded with its own endpoint:

```
curl -F "file=@ferp-printer-com-v44.0.0.1.bin" http://<printer-ip>/updatePrinterFirmwareBin
```

## 1. Layout

```
ferp-printer-<com|usb>-v1/
  app/                         app.cpp is identical in both products; product
    app.cpp                    differences live in app_hw_config.h
    app_hw_config.h            transport, pins, OTA target, MQTT dev type, HTTP port
    app_config.h               app_config_t + CFG_KEY_PRN_* (0x7100 range)
    version.h, user_config.h, app_msg_ids.h, app_msg_table.h, app_module_ids.h, …
  ferp-printer-*-idf/          ESP-IDF project (partitions.csv identical to every FERP product)
    data/                      legacy web pages (index, deviceConfPrinter, styles…)
```

Shared code added for the printers:

| Path | What |
|---|---|
| `app-modules/module_printer/ModulePrinter` | Job queue (own task), bill counter, buzzer, status |
| `app-modules/module_printer/receipt_renderer` | ESC/POS byte stream: themes 1/2/3, totalizer, info slip (port of legacy `printer.cpp`; pure C++, host-testable) |
| `app-modules/module_printer/printer_transport_{uart,usb}` | `IPrinterTransport` implementations |
| `app-modules/module_printer/ModulePrinterApi` | Legacy HTTP API routes |
| `app-modules/module_cloud_print/ModuleCloudPrint` | Cloud registration + print queue client |
| `app-messages/printer/` | Printer message group 0x0060–0x0066 |
| `app-messages/mqtt/msg_mqtt_{subscribe,ext_data}` | Extra MQTT subscriptions (generic) |

Generic upgrades to shared modules (no behaviour change for ferp-com):

- **ModuleMqtt:** `set_dev_type()`, `set_outbound_msgs()`, and extra topic subscriptions via `MsgMqttSubscribe` / `MsgMqttExtData`.
- **ModuleWebServer:**
  - `set_port()`, `set_extra_routes()`, `set_fw_upload_aliases()`;
  - config POST buffers moved off the httpd stack (they overflowed it);
  - OTA progress throttled to 1 % steps, and a reliable complete notification.
- **ModuleConfig:** JSON buffer size set by `MODULE_CONFIG_JSON_BUF_SIZE`; hot reload now re-allocates the buffer.
- **PAL HTTP client:** `pal_http_client_set_body_method()` (PUT / PATCH / DELETE); ModuleHttp uses it.

## 2. Modules and tasks

| Task | Modules |
|---|---|
| storage_task | Spiffs, Config, DeviceInfo |
| timing_task | Ticker, Timer |
| indicator_task | Sysmon |
| printer_task | Printer, PrinterApi |
| network_task | Wifi, Internet, Mqtt, WebClientOta, WebServer, Ota, UdpLog, CloudPrint |
| http_task | Http |

No DispTap, fuel, SD, RTC, CubeSphere or LEDs.

## 3. Interfaces

**HTTP, port 80** (ModuleWebServer):

- **Legacy printer API:**
  - `POST /print`, `POST /print-totalizer`
  - `GET /printSample`, `/erasePrintCount`, `/getDeviceInformation`, `/getMainThreadCounter`, `/reboot`
  - `GET /getDeviceConfigurations`, `POST /setDeviceConfigurationsPost`
  - `POST /updatePrinterFirmwareBin`
  - static pages
- **Standard routes:** `/api/config`, `/api/status`, `/api/ota/*`, `/api/messages`.

**MQTT:** `ferp/ferp-printer/<group>/<mac>/{cmd,resp,evt,ota/*}`, plus UUID topics once the printer is registered with the cloud.

**Messages:**

| ID | Message | Direction |
|---|---|---|
| 0x0060 | MsgPrintJob | → ModulePrinter (reply MsgPrintResult) |
| 0x0061 | MsgPrintResult | ← job outcome, bill count |
| 0x0062 | MsgPrinterCmd | sample / reset count / info slip / beep (reply MsgPrinterStatus) |
| 0x0063 | MsgPrinterGetStatus | → MsgPrinterStatus |
| 0x0064 | MsgPrinterStatus | transport, ready, bill count, job counters |
| 0x0065 | MsgCloudPrintGetStatus | → MsgCloudPrintStatus |
| 0x0066 | MsgCloudPrintStatus | state, queue id, notify topic, counters |

## 4. Configuration (`Configs/DeviceConfigs.json`)

Network keys use the same names as the ferp-com products. The receipt keys use
the **legacy printer names**, so an upgraded printer keeps its settings:

```
name_l1/2/3, add_l1/2/3, tele, thank_l1..6, end_lines, baud_rate, en_print_t,
en_signature, theme, en_cut, lines_aftr_cut, printer_dots, en_cloud_print
```

New keys:

- `footer`: bottom line;
- `cp_poll_s`: cloud queue fallback poll, 60 s;
- `en_boot_print`: info slip at start-up.

The bill counter stays in `Logs/stats.json`, the same file and format as the legacy firmware.

## 5. Cloud print

1. Registration uses the same SAS-AC1 flow as CubeSphere:
   - bootstrap → device_id/secret → `device/config`;
   - read `printer-queue-id` and `printer-notify-topic` from the reply;
   - write the device UUID into DeviceInfo.
2. ModuleMqtt subscribes the notify topic. Each notification polls the queue, and `cp_poll_s` is the fallback interval.
3. Polling: `GET …/queue/<id>/messages`. HTTP 400 `EMPTY_QUEUE_ERROR` means the queue is empty.
4. One message becomes MsgPrintJob, then MsgPrintResult:
   - **OK:** `PATCH {"status":"ACK"}`;
   - **unprintable content:** `REJECTED`;
   - **printer failure:** no ACK; the message is retried after 30 s, without reprinting once it has printed.

## 6. Legacy bugs fixed

- `/erasePrintCount` reset only the file. It now resets the counter in RAM too.
- Cloud jobs were ACKed even when printing failed.
- The config POST acted on every body chunk.
- Unbounded `sprintf` into 256-byte buffers.
- Theme 2 printed a garbage date when the timestamp didn't parse.
- `/printSample` assigned an int to a String.
- The USB example freed the transfer while it was still in flight and padded with 0xAA.

## 7. Build / release

```
build.win.bat --product printer-com --buildall        (or printer-usb)
python release.py --product printer-com --release
```

Bundles carry the product's own target name: a ferp-com bundle can't be flashed
onto a printer, and a printer bundle can't be flashed onto a ferp-com unit.
