# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

HomeKit-compatible smart sprinkler/watering control system for ESP32. Built on ESP-IDF 5.4.1 with the HomeKit Accessory Protocol (HAP) SDK.

## Build & Flash Commands

All commands use the ESP-IDF `idf.py` tool:

```bash
idf.py build                                    # Compile project
idf.py -p /dev/ttyUSB0 -b 460800 erase_flash   # Erase device (first-time only)
idf.py -p /dev/ttyUSB0 -b 460800 flash         # Flash firmware
idf.py -p /dev/ttyUSB0 monitor                  # View serial output
idf.py fullclean                                # Remove all build artifacts
```

VSCode tasks (`.vscode/tasks.json`) also provide these as task shortcuts.

## Architecture

### Application Entry Point (`main/`)
- `app_main.cpp` — Initializes all hardware (I2C, OLED, MCP23017, ADC), creates UI, configures HAP accessory (name: "Sprinkler", model: S1, CID: HAP_CID_SPRINKLER), registers HAP read/write callbacks. Note: `hap_start()` and `app_wifi_handler_start()` are currently commented out pending feature completion.
- `app_wifi_handler.cpp` — WiFi provisioning via BLE (with QR code) or SoftAP, plus hardcoded credential fallback for development.

### Components (`components/`)

| Component | Purpose |
|-----------|---------|
| `output/` | `OutputChannel` (single valve, 2 scheduled events) + `OutputChannels` (2-channel container). Settings persisted to NVS namespace "storage". |
| `input/` | `MoistInput` — ADC Unit 1 Ch 0 for soil moisture. Currently returns hardcoded 0; ADC init is wired but read is not implemented. |
| `ui/` | Screen-based UI: `HomeScreen`, `SettingsMenu`, `EvtTimeScreen`, `MoistThrScreen`. `UI` class manages button events and screen transitions. |
| `ioexp/` | `MCP23017` — I2C I/O expander driver (addr 0x20, reset GPIO 25, IRQ GPIO 26). Buttons on port A pins 8–11 (SEL, BACK, UP, DOWN). |
| `oled/` | `SSD1306I2C` — 128×64 OLED display driver (addr 0x3C). |
| `iocfg/` | Pin definitions: SDA=GPIO5, SCL=GPIO4, I2C_FREQ=400kHz. |
| `qrcodegen/` | QR code generation for WiFi provisioning (untracked, recently added). |

### HomeKit SDK
`homekit-sdk/` is a git submodule (fork of ESP32 HomeKit SDK). The custom partition table (`partitions_hap.csv`) is required — it adds `sec_cert`, `factory_nvs`, and `nvs_keys` partitions alongside the standard OTA layout.

### Key `sdkconfig.defaults` settings
- `CONFIG_FREERTOS_UNICORE=y` — Single core only
- BLE-only (NimBLE stack) for provisioning
- Custom partition table: `partitions_hap.csv`
- HomeKit setup code configured via menuconfig (`Kconfig.projbuild`), default `111-22-333`

## Code Style

Clang-format with LLVM style, 4-space indentation, 120-character line limit. See `.clang-format`.

## Known Incomplete Areas

- `MoistInput::read()` returns hardcoded 0 — ADC reading not yet implemented
- `hap_start()` and `app_wifi_handler_start()` are commented out in `app_main.cpp`
- Button-triggered factory reset / WiFi reset handlers are commented out
