"""GeoMark entry point - selects base or rover mode from config."""

import argparse
import sys


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="geomark",
        description="GeoMark RTK GPS data collector",
    )
    parser.add_argument(
        "mode",
        choices=["base", "rover"],
        help=(
            "Operating mode: 'base' for the stationary reference station, "
            "'rover' for the field unit"
        ),
    )
    parser.add_argument(
        "--config",
        type=str,
        default=None,
        help="Path to a YAML config file (defaults to config/base.yaml or config/rover.yaml)",
    )

    args = parser.parse_args()

    if args.mode == "base":
        from geomark.base.station import BaseStation

        station = BaseStation(config_path=args.config)
        station.run()
    elif args.mode == "rover":
        from geomark.rover.station import RoverStation

        station = RoverStation(config_path=args.config)
        station.run()
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
