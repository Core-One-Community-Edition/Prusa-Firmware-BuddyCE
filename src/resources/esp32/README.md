# The ESP32 firmware

The firmware is built from source (`lib/esp32-nic`) via Docker as part of the regular build;
no pre-built binaries are checked in to prevent accidentally shipping stale builds.

To use locally built binaries instead, place `uart_wifi.bin`, `bootloader.bin` and
`partition-table.bin` into this directory and configure with `ESP_FW_USE_PREBUILT=ON`.
