# ModuleLeds

**Module ID:** `MODULE_LEDS_ID` (8)  
**Task:** `leds_task` — priority 4, 1 KB stack  
**Status:** ✅ Implemented (basic blink on SPIFFS ready)

## Purpose

Drives LED1 and LED2 via the GPIO PAL. Translates system lifecycle events into
LED patterns using the `hsys_led` peripheral driver.

## Messages

| Direction | Message | ID | Notes |
|-----------|---------|----|-------|
| Subscribes | `MsgSpiffsReady` | `0x0201` | Starts 250 ms blink |

No outgoing messages — all output goes through `pal_gpio`.

## GPIO pins

| Pin | Signal | Board alias |
|-----|--------|-------------|
| 5   | LED1   | `LED1_GPIO` |
| 4   | LED2   | `LED2_GPIO` |

## Behaviour

- `init()` — configures GPIO 4 and 5 as push-pull outputs (low). Initialises
  two `hsys_led_t` instances. Subscribes to `MsgSpiffsReady`.
- On `MsgSpiffsReady` — sets pattern `0b01` (length=2) on both LEDs and starts
  them. Each step is 125 ms (`CUE_RESOLUTION_MS`), giving a 250 ms period
  (2 Hz), 50 % duty cycle, repeat forever.

## LED pattern encoding

`hsys_led_set_pattern(led, cue_pattern, pattern_length, repeat_count)`:

- `cue_pattern` is a bitmask read MSB-first. Bit=1 → LED on, bit=0 → LED off.
- Each step is `CUE_RESOLUTION_MS` = 125 ms.
- `repeat_count = 0xFF` → repeat forever.

| Pattern | Length | Visual |
|---------|--------|--------|
| `0b01`  | 2      | OFF 125ms → ON 125ms → repeat (250 ms / 2 Hz) |
| `0b00001111` | 8 | OFF 500ms → ON 500ms → repeat (1 s / 1 Hz) |
| `0b100` | 3      | ON 125ms → OFF 125ms → OFF 125ms (fast single flash) |

## Dependencies

- `hsys_led` (peripheral) — pattern engine; uses `hsys_soft_timer` internally.
- `hsys_soft_timer` (OS) — macOS: `std::thread`-based; ESP32: FreeRTOS timer.
- `pal_gpio` — hardware output; macOS sends `SIM_GPIO_OUT` JSON to the Python UI.

## Simulator UI

The Python UI receives `{"id":"SIM_GPIO_OUT","data":{"pin":5,"level":1,"name":"LED1"}}`
and updates the **LED1** / **LED2** circles in the System panel in real time.

## Planned extensions (TODO_modules.md §12)

- Subscribe to `MsgWifiEvent` → WiFi-connected pattern.
- Subscribe to `MsgInternetStatus` → internet-connected pattern.
- Subscribe to `MsgNozzleState` → pumping pattern.
- Subscribe to `MsgCloudStatus` → cloud-connected pattern.
- Subscribe to `MsgOtaEvent` → OTA-in-progress pattern.
