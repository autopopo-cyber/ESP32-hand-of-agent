# ESP32S3 ODA HID Bridge

ESP32S3 USB composite device firmware: HID Keyboard + HID Mouse + CDC ACM, with ST7789 172x320 LCD status display.

Part of the ODA (Observe-Decide-Act) RPA system.

## Hardware

- **Board:** Waveshare ESP32-S3-LCD-1.47 (USB-A dongle)
- **MCU:** ESP32-S3R2
- **Display:** ST7789 172x320, 3-wire SPI
- **USB:** USB OTG (composite: CDC ACM + HID Keyboard + HID Mouse)

## Build

```bash
./build.sh
```

Requires ESP-IDF v5.4+ with esp_tinyusb component.

## Flash

Hold BOOT button while plugging in, then:

```bash
idf.py -p <port> flash
```

## CDC Protocol

14 command opcodes for HID control over CDC ACM serial. See `main/cdc_protocol.h`.

## Driver

On Windows, install the CDC ACM driver from `oda_cdc.inf`.
