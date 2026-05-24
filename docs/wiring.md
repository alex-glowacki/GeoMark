# GeoMark — Wiring & Pinouts

> ⚠️ This document will be populated once physical hardware is available for testing.

## UM980 → Raspberry Pi Zero 2 W (UART)

| UM980 Pin | Pi GPIO | Notes                        |
|-----------|---------|------------------------------|
| TX        | GPIO 15 (RXD) | Pi receives UM980 output |
| RX        | GPIO 14 (TXD) | Pi sends commands to UM980 |
| GND       | GND     |                              |
| VCC (3.3V)| 3.3V    | Verify UM980 module voltage  |

## SiK Radio → Raspberry Pi Zero 2 W (USB Serial)

The Holybro SiK V3 radio connects via USB and appears as `/dev/ttyUSB0`.
No additional wiring required — powered and communicated through USB.

## Power

Both units are powered independently via 10,000 mAh USB power banks connected
to the Pi's micro-USB power port.