# LilyGO T-Display-S3 Multi-App OS (Clock, CGM & Settings)

A modular multi-app firmware for the **LilyGO T-Display-S3** (ESP32-S3, 1.9" ST7789 170x320 IPS LCD, 16MB Flash, 8MB PSRAM).

---

## Features

- **App Launcher**:
  - Interactive menu designed for the 170x320 portrait resolution.
  - Zero-flicker double-buffered rendering powered by PSRAM sprites (`TFT_eSprite`).
- **Dexcom CGM App**:
  - Direct integration with Dexcom Share API (`shareous1.dexcom.com` and `share1.dexcom.com`).
  - Real-time blood glucose (mg/dL), directional trend arrows (`^^`, `^`, `/^`, `->`, `\v`, `v`, `vv`), delta change, and time elapsed.
  - 2-hour mini trend history graph with target range lines (70 & 180 mg/dL).
  - Background polling decoupled via FreeRTOS task to prevent UI lockup.
- **Clock App**:
  - Automatic SNTP network time sync (`pool.ntp.org`).
  - Day of week, full date, 7-segment digital time, seconds display, and live Wi-Fi RSSI status.
- **Settings & Captive Portal**:
  - Onboard settings viewer with device IP, timezone, and Dexcom server configuration.
  - Built-in **`T-Display AP`** captive portal for configuring Wi-Fi and Dexcom credentials without erasing flash.
- **Hardware Button Controls**:
  - **Button 1 (GPIO 0 / BOOT)**: Cycle through apps.
  - **Button 2 (GPIO 14 / KEY1)**: Launch / Select active app.
  - **Both Buttons Pressed (Chord)**: Universal instant exit back to the Launcher.
- **Safety**:
  - Complete 16MB stock firmware backup included in `backup/firmware_backup_original.bin`.
  - Convenient `./restore_firmware.sh` script to revert to factory state anytime.

---

## Hardware Specifications & Pinout

| Function | Pin / GPIO | Notes |
| :--- | :--- | :--- |
| **LCD Power Enable** | `GPIO 15` | Must be set HIGH to power the ST7789 |
| **LCD Backlight** | `GPIO 38` | PWM / Digital control |
| **ST7789 8-bit Parallel** | `D0..D7`: `39, 40, 41, 42, 45, 46, 47, 48` | High-speed parallel interface |
| **Control Pins** | `CS: 6`, `DC: 7`, `WR: 8`, `RD: 9`, `RST: 5` | Display bus signals |
| **Button 1 (BOOT)** | `GPIO 0` | Cycle Next App |
| **Button 2 (KEY1)** | `GPIO 14` | Select / Enter |
| **Battery ADC** | `GPIO 4` | Battery voltage monitoring |

---

## Build & Upload

Built using [PlatformIO](https://platformio.org):

```bash
# Build firmware
pio run -e t-display-s3

# Upload to device
pio run -e t-display-s3 -t upload --upload-port /dev/cu.usbmodem2101

# Monitor serial debug
pio device monitor -b 115200
```

---

## Firmware Backup & Restore

```bash
# Backup full 16MB flash:
./backup_firmware.sh

# Restore factory firmware:
./restore_firmware.sh backup/firmware_backup_original.bin
```
