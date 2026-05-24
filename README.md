# GeoMark

Self-contained RTK GPS base & rover system for centimeter-accurate field surveying.

Built on Raspberry Pi Zero 2 W + Unicorecomm UM980 GNSS receivers, communicating
over a 915 MHz SiK radio link. No internet connection required in the field.

## Hardware

- 2× Unicorecomm UM980 GNSS receiver (MJRTK module)
- 2× SparkFun SPK-6E tri-band (L1/L2/L5) helical antenna
- 2× Raspberry Pi Zero 2 W
- 1× Holybro SiK V3 915 MHz telemetry radio pair
- 2× 10,000 mAh USB power bank

## Installation

\`\`\`bash
pip install -e ".[dev]"
\`\`\`

## Usage

**Base station:**
\`\`\`bash
geomark base --config config/base.yaml
\`\`\`

**Rover station:**
\`\`\`bash
geomark rover --config config/rover.yaml
\`\`\`

## Development

Run tests:
\`\`\`bash
pytest
\`\`\`

Lint:
\`\`\`bash
ruff check src tests
\`\`\`

## Documentation

- [Architecture](docs/architecture.md)
- [Wiring & Pinouts](docs/wiring.md)
- [Field Guide](docs/field-guide.md)

## License

MIT