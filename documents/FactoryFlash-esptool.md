# Flashing the FERP-COM v3 factory image with esptool.exe

This guide updates a FERP-COM **v3** board (2602, ESP32 DisplayTap) with the
factory image over USB.

## 1. Download

- **esptool.exe** — from Lark:
  <https://saslk-org.jp.larksuite.com/wiki/DNyzw4fDFiSWvgkd1vIjZCIjpPd?fromScene=spaceOverview#share-RinSdbkn5owHvFxffwljif56peM>
- **Factory image** — the latest binaries on Lark:
  <https://saslk-org.jp.larksuite.com/wiki/DNyzw4fDFiSWvgkd1vIjZCIjpPd>
  — file `ferp-esp32-factory-v3.3.0.x.bin`
  - Its size is about **4 MB (4,194,304 bytes)** — the full flash size of the
    device. A file much smaller than that is not the factory image.

Put both files in the same folder and open Command Prompt there.
In the commands below replace `COM9` with your board's port (Device Manager →
Ports).

## 2. Erase the full flash

```bat
esptool.exe --chip esp32 --port COM9 erase_flash
```

## 3. Flash the factory image

The start address must be **0x0**:

```bat
esptool.exe --chip esp32 --port COM9 --baud 460800 write_flash 0x0 ferp-esp32-factory-v3.3.0.3.bin
```

When it finishes (`Hash of data verified.`), press RESET or power-cycle the
board.

## Questions and troubleshooting

| Question / problem | Answer / fix |
|---|---|
| `Failed to connect to ESP32: No serial data received` | The board is not in download mode. Hold **BOOT**, press and release **RESET** (or power-cycle), release **BOOT**, then run the command again. Also check the COM port. |
| `could not open port 'COM9': Access is denied` | Another program is using the port. Close serial monitors and other flash tools. |
| Which COM port is the board? | Device Manager → Ports (COM & LPT). The port appears when the USB cable is plugged in. |
| The factory file is much smaller than 4 MB | It is not the factory image (it is the app-only `.bin`). Download `ferp-esp32-factory-v3.3.0.x.bin`. |
| Can I flash the factory image to `0x1000` or `0x10000`? | No. The factory image always starts at **0x0**. |
| Is the erase step needed? | Yes, erase the full flash first so no old firmware or boot selection is left behind. |
| After flashing, the serial monitor shows `waiting for download` | BOOT is still held, or IO0 is tied low. Release BOOT / remove the jumper and press RESET. |
| The board resets again and again right after start | Disconnect the flashing adapter and power-cycle the board. |
| Flashing stops with timeouts or `Invalid head of packet` | Use a slower speed: `--baud 115200`, and a short USB cable. |
| Are the site settings kept? | No. The factory image restores default settings (Wi-Fi `FERP-SSID` / `FERP-PASSWORD`). Set the site settings again over MQTT. |
| Is this guide for v2 (2404 / 2308) boards? | No, this guide is for **v3** (2602) boards only. |
