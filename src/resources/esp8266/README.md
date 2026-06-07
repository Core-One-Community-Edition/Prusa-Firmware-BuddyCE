# The ESP8266 firmware

The firmware is built from source (`lib/esp8266-nic`) via Docker as part of the regular build;
no pre-built binaries are checked in to prevent accidentally shipping stale builds.

To use locally built binaries instead, place `uart_wifi.bin`, `bootloader.bin` and
`partition-table.bin` into this directory and configure with `ESP_FW_USE_PREBUILT=ON`.

Flasher stub comes from <https://github.com/prusa3d/esptool/>.
Current version is built from `5583f160d9ddb9f07f73c0dc696c8d7a52db2390`.
