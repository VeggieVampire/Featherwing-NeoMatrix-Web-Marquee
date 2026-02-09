# ESP8266 NeoMatrix Web Ticker (FeatherWing 4x8)

A tiny WiFi-controlled scrolling text sign using a HUZZAH Feather ESP8266 and the Adafruit NeoPixel FeatherWing 4x8 (NeoMatrix).  
Set the message from a web page or Serial, customize per-letter colors, and rename the device (hostname) from the web UI. Settings are saved to EEPROM and the device supports mDNS (`http://devicename.local/`).

## Demo Video

🎥 **YouTube Demo:** https://www.youtube.com/shorts/VOLrx4vGLnM

## Battery Stand (3D Print)

Want a clean, stable setup? I made a matching stand for an LP503035 battery.

🧩 **Printables:** https://www.printables.com/model/1592202-feathermarquee-lp503035-battery-stand  

## Features

- Scroll a text message on a 4x8 NeoPixel matrix
- Set message from:
  - Web UI (ESP8266 web server)
  - Serial at 115200 (send a line, it becomes the message)
- Per-letter color palette (each non-space letter advances to the next palette color)
- Edit palette from the Web UI (comma-separated hex like `FF0000,00FF00,0000FF`)
- Palette saved in EEPROM
- Rename device (hostname) from Web UI
- Hostname saved in EEPROM
- mDNS support: `http://<devicename>.local/`
- Web controls for:
  - Scroll speed
  - Brightness
  - Extra spacing
  - Bold mode
  - Y offset
  - Hard clear (helps if you see trails)

## Hardware

- Adafruit HUZZAH Feather ESP8266
- Adafruit NeoPixel FeatherWing 4x8 (NeoMatrix)
- Uses NeoPixel data pin: GPIO15

Default matrix config in this sketch:
- Corner: TOP + RIGHT
- Layout: COLUMNS
- Pattern: PROGRESSIVE
- Rotation: 2

## Getting Started

### 1) Install libraries

In Arduino IDE (Library Manager), install:
- Adafruit GFX Library
- Adafruit NeoMatrix
- Adafruit NeoPixel
- ESP8266 board support (via Boards Manager)

### 2) Add WiFi credentials

Create a file named `secrets.h` in the same folder as the `.ino`:

```c
#pragma once
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
