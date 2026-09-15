# ESP32-C5-MINI-1 — RGB LED (WS2812B)

ESP-IDF project that turns on an XL-5050RGBC-WS2812B addressable RGB LED
connected to **GPIO8** of an **ESP32-C5-MINI-1** module.

## Hardware

- Board/module: ESP32-C5-MINI-1
- LED: XL-5050RGBC-WS2812B (WS2812B-compatible, single-wire data protocol)
- Wiring:
  - LED `DIN` → GPIO8
  - LED `VDD` → 3.3V (or 5V, per the LED's datasheet)
  - LED `GND` → GND
  - A ~300–500 Ω series resistor on the data line and a decoupling
    capacitor (100 µF+) across VDD/GND at the LED are recommended.

The LED is driven over the RMT peripheral using Espressif's managed
`led_strip` component, so no bit-banging is needed.

## Prerequisites

1. Visual Studio Code with the **Espressif IDF** extension installed.
2. ESP-IDF **v5.3 or newer** installed via the extension's setup wizard
   (required for ESP32-C5 target support).

## Opening the project

1. Open this folder in VS Code (`File > Open Folder...`).
2. Run **ESP-IDF: Set Espressif Target** and choose `esp32c5`.
3. Run **ESP-IDF: Select Port to Use** and pick the serial port your
   board enumerates as.
4. Run **ESP-IDF: Build your Project** (this also fetches the
   `led_strip` managed component declared in `main/idf_component.yml`).
5. Run **ESP-IDF: Flash your Project**, then **ESP-IDF: Monitor your
   Device** to see log output.

## Command-line equivalent

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32c5
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## What it does

`main/main.c` initializes the RMT-backed `led_strip` driver on GPIO8
and turns the LED on to a dim white color. Change the values passed to
`led_strip_set_pixel()` to set a different color.
