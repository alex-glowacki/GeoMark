"""NMEA sentence parser - wraps pynmea2 for GeoMark's data model."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import pynmea2


@dataclass
class GNSSFix:
    """Parsed position fix from a GGA sentence."""

    latitude: float
    longitude: float
    altitude: float
    fix_quality: int  # 0=invalid, 1=GPS, 4=RTK fixed, 5=RTK float
    satellites: int
    hdop: float
    timestamp: str


def parse_gga(sentence: str) -> GNSSFix | None:
    """Parse a GGA NMEA sentence into a GNSSFix.

    Args:
        sentence: Raw NMEA sentence string (e.g. '$GPGGA,...).

    Returns:
        A GNSSFix dataclass, or None if the sentence is invalid or not GGA.
    """
    try:
        msg = pynmea2.parse(sentence)
    except pynmea2.ParseError:
        return None

    if msg.sentence_type != "GGA":
        return None

    gga: Any = msg  # pynmea2 has no type stubs - cast to Any to suppress Pylance warnings

    return GNSSFix(
        latitude=float(gga.latitude),
        longitude=float(gga.longitude),
        altitude=float(gga.altitude),
        fix_quality=int(gga.gps_qual),
        satellites=int(gga.num_sats),
        hdop=float(gga.horizontal_dil),
        timestamp=str(gga.timestamp),
    )


def is_rtk_fixed(fix: GNSSFix) -> bool:
    """Return True if the fix quality indicates a full RTK fixed solution."""
    return fix.fix_quality == 4


def is_rtk_float(fix: GNSSFix) -> bool:
    """Return True is the fix quality indicates an RTK float solution."""
    return fix.fix_quality == 5
