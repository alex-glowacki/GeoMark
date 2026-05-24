"""Tests for the NMEA sentence parser."""

from geomark.gnss.nmea import is_rtk_fixed, is_rtk_float, parse_gga

# GGA sentence with RTK fixed quality (fix_quality=4)
SAMPLE_GGA_FIXED = (
    "$GPGGA,123519,4807.038,N,01131.000,E,4,08,0.9,545.4,M,46.9,M,,*47"
)

# GGA sentence with no fix (fix_quality=0)
SAMPLE_GGA_NO_FIX = (
    "$GPGGA,123519,0000.000,N,00000.000,E,0,00,99.9,0.0,M,0.0,M,,*6A"
)


def test_parse_gga_returns_fix() -> None:
    fix = parse_gga(SAMPLE_GGA_FIXED)
    assert fix is not None
    assert fix.fix_quality == 4
    assert fix.satellites == 8
    
    
def test_parse_gga_invalid_returns_none() -> None:
    assert parse_gga("not a sentence") is None
    
    
def test_is_rtk_fixed() -> None:
    fix = parse_gga(SAMPLE_GGA_FIXED)
    assert fix is not None
    assert is_rtk_fixed(fix) is True
    
    
def test_is_rtk_float() -> None:
    fix = parse_gga(SAMPLE_GGA_FIXED)
    assert fix is not None
    assert is_rtk_float(fix) is False