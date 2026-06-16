# GeoMark

A self-contained two-unit RTK GNSS surveying system — a personal-build
alternative to professional instruments like Trimble or Leica.

## Overview

GeoMark consists of two units that work together to achieve
centimeter-accurate field positioning:

- **Base station (`geomark-base`)** — fixed at a known or self-surveyed point,
  receives GNSS signals, and broadcasts RTCM3 correction data to the rover via
  SiK radio
- **Rover (`geomark-rover`)** — pole-top unit; receives GNSS signals plus base
  corrections, computes RTK positions, and streams data to the handheld over WiFi
- **Handheld (`geomark-handheld`)** — Pi 5 running `--mode ui`; connects to
  rover AP, displays TFT UI, captures survey points, exports CSV

The UM980 GNSS module handles the full RTK engine onboard (carrier phase
ambiguity resolution, Kalman filtering). GeoMark's C code is the management
and relay layer above that — serial I/O, protocol parsing, correction relay,
data collection, and UI.

End-to-end RTK test completed outdoors. Full fix progression confirmed:
`SINGLE → DGPS → FLOAT → RTK FIXED`. Test point captured via TFT UI.

## Hardware

| Component | `geomark-base` | `geomark-rover` | `geomark-handheld` |
|-----------|---------------|----------------|-------------------|
| SBC | Raspberry Pi Zero W | Raspberry Pi Zero 2 W | Raspberry Pi 5 |
| Hostname | `geomark-base.local` | `192.168.10.1` (rover AP) | — |
| Username | `pi` | `pi` | `alex` |
| OS | Raspberry Pi OS Lite 32-bit | Raspberry Pi OS Lite 64-bit | Raspberry Pi OS |
| GNSS module | Unicorecomm UM980 | Unicorecomm UM980 | — |
| Antenna | AK159 (L1/L2/L5 tri-band) | AK159 (L1/L2/L5 tri-band) | — |
| Radio | Holybro SiK V3 915 MHz | Holybro SiK V3 915 MHz | — |
| Display | — | Hosyond 4.0" 480×320 TFT (ST7796S) | — |

> **SiK radio note:** Both radios must have `MAVLINK=0` (`ATS6=0`). This is
> set via AT command mode and written to flash with `AT&W`. Do not revert.

> **Display note:** The ST7796S is a 5V Arduino shield. A 3.3V↔5V logic
> level shifter is required on all SPI lines.

## Architecture

```
geomark --mode base   →  reads UM980, relays RTCM3 corrections via SiK radio
geomark --mode rover  →  receives corrections, computes RTK, streams to handheld
geomark --mode ui     →  connects to rover AP, renders TFT UI, captures points
```

Single compiled binary. Mode selected at runtime via `--mode` flag or config file.

### Language & Build

- **Language:** C (C11), `_GNU_SOURCE` required for Linux extensions
- **Build system:** CMake + Ninja
- **No third-party libraries** — all parsers and drivers written from scratch
- **RTKLIB conventions** followed for protocol parsing

### WiFi Networking

`geomark-rover` runs a WiFi AP (`geomark-rover` SSID, `geomark2024`).
`geomark-handheld` connects via nmcli profile `geomark-client` (`192.168.10.1/24`).
Stream server on rover: TCP port 4500. Wire format: 44-byte `RoverPacket`,
magic `0x474D524B`.

### SSH Access

```bash
# WSL2 / home network → geomark-base
ssh geomark-base          # alias via ~/.ssh/config → pi@geomark-base.local

# geomark-handheld → geomark-rover (connects AP first)
sudo nmcli connection up geomark-client
ssh geomark-rover         # alias via ~/.ssh/config → pi@192.168.10.1
```

Key auth is configured on `geomark-handheld` (`~/.ssh/id_ed25519`).
No password required for either device.

## Project Structure

```
GeoMark/
├── CMakeLists.txt
├── CMakePresets.json
├── .clang-format
├── cmake/
│   ├── arm-linux-gnueabihf.cmake     # geomark-base toolchain (ARMv6)
│   └── aarch64-linux-gnu.cmake       # geomark-rover toolchain (AArch64)
├── include/
│   └── geomark.h                     # Shared types, return codes, version
├── src/
│   ├── main.c                        # Entry point, mode dispatch
│   ├── stream/                       # serial, radio, file I/O
│   ├── gnss/                         # nmea, rtcm3, um980 driver
│   ├── base/                         # base station logic
│   ├── rover/                        # rover station logic
│   ├── collector/                    # point storage, coords, export
│   ├── survey/                       # survey session, codelist, CSV export
│   ├── net/                          # rover_packet, stream_server, stream_client
│   ├── ui/                           # client (handheld UI mode)
│   │   └── tft/                      # ST7796S display, XPT2046 touch
│   └── util/                         # config, logging, threads
├── tests/                            # Unit tests (no hardware required)
├── systemd/                          # Service units
├── scripts/
│   └── deploy.ps1                    # PowerShell SSH deploy script
└── docs/
    ├── project-summary.md
    ├── wifi-ap-setup.md
    └── ssh-setup.md
```

## Build

### Prerequisites

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

### Cross-compile for `geomark-base` (Pi Zero W, ARMv6 32-bit)

```bash
cmake --preset pi-base-release
cmake --build build/pi-base
```

### Cross-compile for `geomark-rover` (Pi Zero 2 W, AArch64 64-bit)

```bash
cmake --preset pi-rover-release
cmake --build build/pi-rover
```

### Update on device (requires internet)

```bash
# On geomark-base or geomark-rover:
cd ~/GeoMark && git pull && rm -rf build && cmake --preset host-debug && cmake --build build/host

# On geomark-rover (toggle home WiFi):
sudo nmcli connection up home-wifi
cd ~/GeoMark && git pull && rm -rf build && cmake --preset host-debug && cmake --build build/host
sudo nmcli connection down home-wifi
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

## Runtime

```bash
geomark --mode base   --config /etc/geomark/geomark.conf
geomark --mode rover  --config /etc/geomark/geomark.conf
geomark --mode ui     --host 192.168.10.1
```

## Phase Progress

| Phase | Modules | Status |
|-------|---------|--------|
| 0 — Environment & scaffold | CMake, directory skeleton | ✅ Complete |
| 1 — Serial + GNSS parsing | `stream/serial`, `gnss/nmea`, `gnss/rtcm3`, `gnss/um980` | ✅ Complete |
| 2 — Collector | `collector/`, `util/config`, `util/thread` | ✅ Complete |
| 3 — Base & rover station logic | `base/station`, `rover/station` | ✅ Complete |
| 4 — TFT UI (spidev) | `ui/tft/display`, `ui/tft/touch` | ✅ Complete |
| 5 — Survey session & CSV export | `survey/` | ✅ Complete |
| 6 — WiFi AP networking layer | `net/rover_packet`, `net/stream_server`, `net/stream_client` | ✅ Complete |
| End-to-end RTK test (outdoors) | — | ✅ Complete |
| **Field hardening** | systemd, SSH, auto-connect, OTA update, WiFi manager | 🔄 In progress |

## CI

GitHub Actions runs on every push and pull request to `main`:

- **Host build & test** — native GCC, all unit tests must pass
- **Cross-compile base** — ARMv6 32-bit, must link cleanly
- **Cross-compile rover** — AArch64 64-bit, must link cleanly

## References

- [RTKLIB](https://github.com/rtklibexplorer/RTKLIB) — primary C reference
- [Linux spidev](https://www.kernel.org/doc/html/latest/spi/spidev.html)
- [Holybro SiK V3 LED docs](https://docs.holybro.com/radio/sik-telemetry-radio-v3/led-and-connection)
- [LCD wiki ST7796](https://www.lcdwiki.com/4.0inch_SPI_Module_ST7796)
- [Unicore N4 Commands Reference](https://en.unicore.com/uploads/file/Unicore%20Reference%20Commands%20Manual%20For%20N4%20High%20Precision%20Products_V2_EN_R1.4.pdf)
- POSIX termios — `man termios`
- UM980 user manual — on hand

## License

MIT