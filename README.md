# ESP32 tuning-module BLE gateway (ESP-IDF)

ESP-IDF / VS Code port of the original Arduino sketch (`TS2_pro3.1.ino`) that
runs on an ESP32-WROOM-32E. The firmware bridges a BLE Low Energy connection
from the companion phone app to an ATmega16M1 power-tuning module over a
hardware UART, and persists the current settings (warm-up time, ECO/SPORT/RACE
state, ON/OFF, boost level, and two selector values) across reboots.

This is a like-for-like port: the BLE service/characteristic layout, the
serial protocol with the ATmega and the setting-persistence logic are
unchanged from the original `.ino`. Only the framework-specific pieces were
translated from the Arduino core to plain ESP-IDF:

| Arduino (.ino)                                   | ESP-IDF (this project)                                   |
|---------------------------------------------------|------------------------------------------------------------|
| `BLEDevice`/`BLEServer`/`BLECharacteristic`/`BLE2902` (arduino-esp32 BLE lib) | [`h2zero/esp-nimble-cpp`](https://github.com/h2zero/esp-nimble-cpp) (`NimBLEDevice`/`NimBLEServer`/`NimBLECharacteristic`). CCCD (0x2902) descriptors are created automatically for `NOTIFY`/`INDICATE` characteristics, so no `BLE2902` equivalent is needed. |
| `HardwareSerial SerTED(2)` (UART2, TX=17, RX=16)   | `driver/uart.h`, `UART_NUM_2` on the same GPIO17/GPIO16 pins |
| `EEPROM.read/write/commit`                         | NVS (`nvs_get_u8`/`nvs_set_u8`/`nvs_commit`)                |
| `setup()` / `loop()`                                | `app_main()`                                                |
| `delay(ms)`                                         | `vTaskDelay(pdMS_TO_TICKS(ms))`                             |
| `Serial.print*`                                     | `ESP_LOGI`                                                  |
| `ESP.restart()`                                     | `esp_restart()`                                             |

## Project layout

```
.
├── CMakeLists.txt            # top-level ESP-IDF project file
├── sdkconfig.defaults        # NimBLE host enabled, peripheral-only role
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml     # pulls in h2zero/esp-nimble-cpp via the component manager
    └── main.cpp              # ported gateway logic
```

## Building (VS Code + ESP-IDF extension, or CLI)

Requires ESP-IDF v5.0 or newer (the `h2zero/esp-nimble-cpp` component targets
the v2.x NimBLE-CPP API, which needs IDF 5.x).

```sh
idf.py set-target esp32
idf.py build
idf.py -p <PORT> flash monitor
```

The `h2zero/esp-nimble-cpp` component is fetched automatically on first build
via the ESP-IDF component manager (`main/idf_component.yml`).

In VS Code, open this folder with the Espressif IDF extension installed, pick
the `esp32` target and a serial port from the status bar, and use the
extension's Build / Flash / Monitor commands (they invoke the same `idf.py`
targets shown above).

## Hardware

- Target: ESP32-WROOM-32E
- UART2 to the ATmega16M1 tuning module: TX = GPIO17, RX = GPIO16, 9600 8N1
- BLE device name: `tune`
