"""NMEA sentence parser - wraps pynmea2 for GeoMark's data model."""

from __future__ import annotations

from dataclasses import dataclass

import pynmea2


@dataclass
class GNSSFix:
    """Parsed position fix from a GGA sentence."""
    
    latitude: float
    longitude: float
    altitude: float
    fix_quality: int    # 0=invalid, 1=GPS, 4=RTK fixed, 5=RTK float
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
    
    if not isinstance(msg, pynmea2.types.talker.GGA):
        return None
    
    return GNSSFix(
        latitude=float(msg.latitude),
        longitude=float(msg.longitude),
        altitude=float(msg.altitude),
        fix_quality=int(msg.gps_qual),
        satellites=int(msg.num_sats),
        hdop=float(msg.horizontal_dil),
        timestamp=str(msg.timestamp),
    )
    
    
def is_rtk_fixed(fix: GNSSFix) -> bool:
    """Return True if the fix quality indicates a full RTK fixed solution."""
    return fix.fix_quality == 4


def is_rtk_float(fix: GNSSFix) -> bool:
    """Return True is the fix quality indicates an RTK float solution."""
    return fix.fix_quality == 5