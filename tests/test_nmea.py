"""Tests for the NMEA sentence parser."""

from geomark.gnss.nmea import is_rtk_fixed, is_rtk_float, parse_gga

# Valid GGA sentence with RTK fixed quality (fix_quality=4), verified checksum
SAMPLE_GGA_FIXED = "$GPGGA,092750.000,5321.6802,N,00630.3372,W,4,8,1.03,61.7,M,55.2,M,,*73"

# Valid GGA sentence with no fix (fix_quality=0), verified checksum
SAMPLE_GGA_NO_FIX = "$GPGGA,092750.000,0000.0000,N,00000.0000,W,0,0,99.9,0.0,M,0.0,M,,*49"


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
