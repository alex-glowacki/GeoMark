# GeoMark — Project Summary

Self-contained two-unit RTK GNSS surveying system (base + rover).  
Full architecture notes: see `2026_06_07_GeoMark_Chat_Summary.md` in repo root.

## Quick Reference

| Unit   | Hardware              | OS                        | Mode flag        |
|--------|-----------------------|---------------------------|------------------|
| Base   | Pi Zero W             | Raspberry Pi OS Lite 32-bit | `--mode base`  |
| Rover  | Pi Zero 2 W           | Raspberry Pi OS Lite 64-bit | `--mode rover` |

## Build

```bash
# Host (WSL2) — dev and test
cmake --preset host-debug
cmake --build build/host
ctest --test-dir build/host

# Cross-compile for base (ARMv6 32-bit)
cmake --preset pi-base-release
cmake --build build/pi-base

# Cross-compile for rover (AArch64 64-bit)
cmake --preset pi-rover-release
cmake --build build/pi-rover
```

## Deploy

```powershell
.\scripts\deploy.ps1 -Target all
```