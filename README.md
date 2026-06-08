# GeoMark

A self-contained two-unit RTK GNSS surveying system — a personal-build
alternative to professional instruments like Trimble or Leica.

## Overview

GeoMark consists of two units that work together to achieve
centimeter-accurate field positioning:

- **Base station** — fixed at a known or self-surveyed point, receives GNSS
  signals, and broadcasts RTCM3 correction data to the rover via SiK radio
- **Rover** — carried by the surveyor, receives GNSS signals plus base
  corrections, computes RTK positions, stores survey points, and provides
  a field UI via dedicated TFT touchscreen

The UM980 GNSS module handles the full RTK engine onboard (carrier phase
ambiguity resolution, Kalman filtering). GeoMark's C code is the management
and relay layer above that — serial I/O, protocol parsing, correction relay,
data collection, and UI.

There is no web server, no phone UI, and no WiFi hotspot. The rover is a
fully self-contained field instrument. Data is retrieved over SSH/SCP when
back at the office.

## Hardware

| Component | Base | Rover |
|-----------|------|-------|
| SBC | Raspberry Pi Zero W | Raspberry Pi Zero 2 W |
| OS | Raspberry Pi OS Lite 32-bit | Raspberry Pi OS Lite 64-bit |
| GNSS module | Unicorecomm UM980 (MJRTK-UM980) | Unicorecomm UM980 (MJRTK-UM980) |
| Antenna | AK159 (L1/L2/L5 tri-band) | AK159 (L1/L2/L5 tri-band) |
| Radio | Holybro SiK V3 915 MHz | Holybro SiK V3 915 MHz |
| Display | — | Hosyond 4.0" 480×320 TFT (ST7796S) |
| Touch | — | XPT2046 (confirm PCB markings) |

> **Hardware note:** The ST7796S display is a 5V Arduino shield. A 6-channel
> 3.3V↔5V bidirectional logic level shifter is required on all SPI lines
> before the display is wired or Phase 4 is implemented. Do not connect
> directly to Pi GPIO.

## Architecture
geomark --mode base   →  reads UM980, relays RTCM3 corrections via SiK radio
geomark --mode rover  →  receives corrections, outputs RTK positions, logs points

Single compiled binary. Mode selected at runtime via `--mode` flag or config file.

### Language & Build

- **Language:** C (C11)
- **Build system:** CMake + Ninja
- **No third-party libraries** — all parsers and drivers written from scratch
- **No web server, no WiFi hotspot**

### Industry Comparison

| Layer | Trimble/Leica | RTKLIB | GeoMark |
|-------|--------------|--------|---------|
| RTK engine | Proprietary C | C | UM980 firmware (onboard) |
| Protocol parsing | C | C | C (Phase 1) |
| Station logic | C++ | C | C (Phase 2) |
| Data collection | C++ | C | C (Phase 3) |
| Field UI | Dedicated touchscreen | Desktop GUI | TFT touchscreen (Phase 4) |

## Project Structure
GeoMark/
├── CMakeLists.txt
├── CMakePresets.json
├── .clang-format
├── cmake/
│   ├── arm-linux-gnueabihf.cmake   # Pi Zero W (base) toolchain
│   └── aarch64-linux-gnu.cmake     # Pi Zero 2 W (rover) toolchain
├── include/
│   └── geomark.h                   # Shared types, return codes, version
├── src/
│   ├── main.c                      # Entry point, mode dispatch
│   ├── stream/                     # Serial, radio, file I/O
│   ├── gnss/                       # NMEA, RTCM3, UM980 driver
│   ├── base/                       # Base station logic
│   ├── rover/                      # Rover logic
│   ├── collector/                  # Point storage, coords, export
│   ├── ui/tft/                     # ST7796S display, XPT2046 touch
│   └── util/                       # Config, logging, threads
├── tests/                          # Unit tests (no hardware required)
├── systemd/                        # Service units for base and rover
├── scripts/
│   └── deploy.ps1                  # PowerShell SSH deploy script
└── docs/
└── project-summary.md

## Build

### Prerequisites

- WSL2 (Ubuntu 24.04) or any Linux x86-64 host
- `gcc`, `cmake`, `ninja-build`
- `gcc-arm-linux-gnueabihf` (base cross-compile)
- `gcc-aarch64-linux-gnu` (rover cross-compile)

```bash
sudo apt install -y gcc cmake ninja-build \
    gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
    gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

### Host (WSL2) — dev and test

```bash
cmake --preset host-debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

### Cross-compile for base (Pi Zero W, ARMv6 32-bit)

```bash
cmake --preset pi-base-release
cmake --build build/pi-base
```

### Cross-compile for rover (Pi Zero 2 W, AArch64 64-bit)

```bash
cmake --preset pi-rover-release
cmake --build build/pi-rover
```

## Deploy

```powershell
# Deploy to both units
.\scripts\deploy.ps1

# Deploy to one unit only
.\scripts\deploy.ps1 -Target base
.\scripts\deploy.ps1 -Target rover

# Deploy and install/enable systemd service
.\scripts\deploy.ps1 -Target all -InstallService
```

Requires SSH key auth set up to `geomark-base.local` and `geomark-rover.local`.

## Runtime

```bash
geomark --mode base  --config /etc/geomark/geomark.conf
geomark --mode rover --config /etc/geomark/geomark.conf
```

## Build Phases

| Phase | Modules | Status |
|-------|---------|--------|
| 0 — Environment & scaffold | CMake, directory skeleton, stubs | ✅ Complete |
| 1 — Serial + GNSS parsing | `stream/serial`, `gnss/nmea`, `gnss/rtcm3`, `gnss/um980` | 🔜 Next |
| 2 — Station logic | `base/station`, `rover/station`, `util/config`, `util/thread` | ⏳ |
| 3 — Data collection | `collector/points`, `collector/coords`, `collector/export` | ⏳ |
| 4 — TFT UI | `ui/tft/display`, `ui/tft/touch` | ⏳ |

## CI

GitHub Actions runs on every push and pull request to `main`:

- **Host build & test** — native GCC, all unit tests must pass
- **Cross-compile base** — ARMv6 32-bit, must link cleanly
- **Cross-compile rover** — AArch64 64-bit, must link cleanly

## References

- [RTKLIB](https://github.com/rtklibexplorer/RTKLIB) — primary C reference
- [Linux spidev](https://www.kernel.org/doc/html/latest/spi/spidev.html)
- POSIX termios — `man termios`
- UM980 user manual — on hand

## License

MIT