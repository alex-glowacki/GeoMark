# GeoMark — Architecture Overview

## System Summary

GeoMark is a self-contained RTK GPS base and rover system for centimeter-accurate
field surveying. It runs on two Raspberry Pi Zero 2 W units, each paired with a
Unicorecomm UM980 GNSS receiver and connected via a 915 MHz SiK telemetry radio link.

## Data Flow

\`\`\`
[UM980 Base] → RTCM3 corrections → [SiK Radio TX]
                                          |
                                     ~wireless~
                                          |
                                   [SiK Radio RX] → [UM980 Rover] → NMEA GGA
                                                                         |
                                                                   [Collector]
                                                                         |
                                                               [CSV / GeoJSON output]
\`\`\`

## Hardware Per Unit

| Component           | Part                        |
|---------------------|-----------------------------|
| Compute             | Raspberry Pi Zero 2 W       |
| GNSS receiver       | Unicorecomm UM980 (MJRTK)   |
| Antenna             | SparkFun SPK-6E (L1/L2/L5) |
| Radio               | Holybro SiK V3 915 MHz      |
| Power               | 10,000 mAh USB power bank   |

## Software Stack

| Layer      | Technology                          |
|------------|-------------------------------------|
| OS         | Raspberry Pi OS Lite                |
| UART pipe  | RTKLIB `str2str`                    |
| App        | Python 3.11+                        |
| Web UI     | FastAPI + Jinja2 (Pi hotspot)       |
| CLI UI     | Rich                                |
| Config     | YAML                                |
| Boot       | systemd service units               |

## Module Breakdown

- `geomark.gnss` — UM980 UART driver, NMEA parser, RTCM3 handler (shared)
- `geomark.base` — Survey-in mode, RTCM3 broadcast
- `geomark.rover` — RTK fix monitoring, RTCM3 injection
- `geomark.collector` — Session management, point model, CSV/GeoJSON export
- `geomark.ui.web` — FastAPI field UI served over Pi WiFi hotspot
- `geomark.ui.cli` — Rich terminal display fallback