"""Tests for the survey session and point data model."""

from datetime import datetime

from geomark.collector.point import SurveyPoint
from geomark.collector.session import SurveySession


def _make_point(name: str = "P1", fix_quality: int = 4) -> SurveyPoint:
    return SurveyPoint(
        name=name,
        latitude=44.97997,
        longitude=93.26384,
        altitude=290.1,
        fix_quality=fix_quality,
        hdop=0.8,
        satellites=14,
        timestamp=datetime.utcnow(),
    )
    
    
def test_session_add_point() -> None:
    session = SurveySession("test-session")
    session.add_point(_make_point("P1"))
    assert len(session) == 1
    
    
def test_session_remove_last() -> None:
    session = SurveySession()
    session.add_point(_make_point("P1"))
    session.add_point(_make_point("p2"))
    removed = session.remove_last()
    assert removed is not None
    assert removed.name == "P2"
    assert len(session) == 1
    
    
def test_session_remove_last_empty() -> None:
    session = SurveySession()
    assert session.remove_last() is None
    
    
def test_point_rtx_fixed() -> None:
    point = _make_point(fix_quality=4)
    assert point.is_rtk_fixed is True
    assert point.is_rtk_float is False
    
    
def test_point_rtk_float() -> None:
    point = _make_point(fix_quality=5)
    assert point.is_rtk_fixed is False
    assert point.is_rtk_float is True