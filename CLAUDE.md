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
- `app_main.cpp` — Initializes all hardware (I2C, OLED, MCP23017, RS485 probe bus), creates UI, configures HAP accessory (name: "Sprinkler", model: S1, CID: HAP_CID_SPRINKLER), registers HAP read/write callbacks. Each configured soil probe becomes a HomeKit humidity sensor + temperature sensor (created at boot; the HAP config number is bumped when the probe set changes). The two zones are plain Valve services (irrigation type). A "Zone N Auto" switch per zone (automatic mode = `OutputChannel::setSchedEnabled`) is added last with fixed instance IIDs (2005+), so adding services never renumbers existing ones — keep it that way (append new services with fixed IIDs). Do not reintroduce an Irrigation System service with linked valves: with it (1.0.7–1.0.12) the Home app listed the zones twice and made them read-only; IIDs 2000–2004 were its and stay unused. Note: `hap_start()` and `app_wifi_handler_start()` are currently commented out pending feature completion.
- `app_wifi_handler.cpp` — WiFi provisioning via BLE (with QR code) or SoftAP, plus hardcoded credential fallback for development.
- Web (port 8080, `WebBase` owns the httpd; every page except `/` has a "← Home" link at the top): `/homekit` (`WebBase`: pairing QR code + setup code, reopen pairing window, remove pairing = `hap_reset_pairings()` + reboot), `/logs` (live device log: `WebLog` keeps the last 8 KB of ESP_LOG output and of printf from tasks started after `weblog::init()`, e.g. the HAP core; the buffer is in `.noinit` RAM so it survives a crash/software restart, and a `--wrap=esp_panic_handler` wrapper saves the crash reason, task and backtrace, logged as "LAST CRASH" on the next boot and shown on `/system`), `/control` (`ControlServer`: valves, schedule, moisture trend), `/events` (`ControlServer`: event log), `/system` (`OtaServer`: status, uptime, firmware upload via POST `/update`, restart; GET `/update` redirects here), `/probes` (`ProbeServer`: probe list, live readings, calibration, address change, bus scan, zone→probe assignment + thresholds, probe firmware update).

### Components (`components/`)

| Component | Purpose |
|-----------|---------|
| `output/` | `OutputChannel` (single valve, 2 scheduled events, assigned probe address + moisture skip threshold) + `OutputChannels` (2-channel container). Settings persisted to NVS namespace "storage". |
| `probe/` | RS485 soil moisture probes (repo `mikegormack/soil-moisture-probe`). `ModbusMaster` — Modbus RTU master on a half-duplex UART (DE on RTS). `ProbeManager` — probe list (max 8, NVS key "probes"), poll task, scan, calibration commands, address change, firmware update job (image staged in the inactive OTA partition). `probe_reg` mirrors the probe's register map. |
| `ui/` | Screen-based UI: `HomeScreen`, `SettingsMenu`, `EvtTimeScreen`, `MoistThrScreen` (per zone), `ProbesScreen`. `UI` class manages button events and screen transitions. |
| `ioexp/` | `MCP23017` — I2C I/O expander driver (addr 0x20, reset GPIO 25, IRQ GPIO 26). Buttons on port A pins 8–11 (SEL, BACK, UP, DOWN). |
| `oled/` | `SSD1306I2C` — 128×64 OLED display driver (addr 0x3C). |
| `iocfg/` | Pin definitions: SDA=GPIO5, SCL=GPIO4, I2C_FREQ=400kHz. RS485 pins are in menuconfig "Soil probes (RS485)": UART2, TX=GPIO17, RX=GPIO16, DE=GPIO18, 9600 8N2, poll 10 s. |
| `qrcodegen/` | QR code generation for WiFi provisioning (untracked, recently added). |

### HomeKit SDK
`homekit-sdk/` is a git submodule (fork of ESP32 HomeKit SDK). The custom partition table (`partitions_hap.csv`) is required — it adds `sec_cert`, `factory_nvs`, and `nvs_keys` partitions alongside the standard OTA layout.

### Key `sdkconfig.defaults` settings
- `CONFIG_FREERTOS_UNICORE=y` — Single core only
- BLE-only (NimBLE stack) for provisioning
- Custom partition table: `partitions_hap.csv`
- HomeKit setup code configured via menuconfig (`Kconfig.projbuild`), default `111-22-333`

### Soil probes
- Probes are Modbus slaves at 9600 8N2, default address 1. Add one new probe at a time on `/probes`, then change its address.
- A zone with a probe skips scheduled runs while moisture ≥ its threshold; without a usable reading (offline, uncalibrated, stale, in bootloader) it waters as scheduled and logs `SCHED_NO_PROBE`.
- Firmware update: upload the probe's `build/firmware/soil_probe.bin` on `/probes`. Uses the bootloader on the probe repo's `develop` branch (firmware ≥ 1.1, bootloader flashed once over SWD): app at 0x08001800, max 24 KB, 128-byte chunks, bootloader reports version 0xB0xx in input reg 9. Protocol: probe `src/boot/boot_regs.h` + `tools/fwupdate.py`; keep `components/probe/ProbeManager.h` (`probe_reg`) in step with them.
- Moisture trend (`main/MoistureTrend.*`, NVS `mtrend`/`zones`): hourly, 30 days, both zones' probe moisture (0.5 % steps) and soil temperature (0.5 °C steps) in 8-byte entries. `/control` charts it.
- Registry components (json_parser, json_generator, jsmn, libsodium, mdns) come from the Espressif component registry via the component manager (`dependencies.lock`).

## Code Style

Clang-format with LLVM style, 4-space indentation, 120-character line limit. See `.clang-format`.

## Known Incomplete Areas

- Probe support is untested on hardware (logic tested on the host against the probe's bootloader code)
- `hap_start()` and `app_wifi_handler_start()` are commented out in `app_main.cpp`
- Button-triggered factory reset / WiFi reset handlers are commented out
