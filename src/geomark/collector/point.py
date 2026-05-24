"""Survey point data model."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime


@dataclass
class SurveyPoint:
    """A single collected survey point.
    
    Attributes:
        name: User-assigned point name or identifier.
        latitude: WGS84 latitude in decimal degrees.
        longitude: WGS84 longitude in decimal degrees.
        altitude: Ellipsoidal height in meters.
        fix_quality: RTK fix quality at time of collection (4=fixed, 5=float).
        hdop: Horizontal dilution of precision.
        satellites: Number of satellites used in the solution.
        timestamp: UTC timestamp of the fix.
        notes: Optional field notes attached to this point.
    """
    
    name: str
    latitude: float
    longitude: float
    altitude: float
    fix_quality: int
    hdop: float
    satellites: int
    timestamp: datetime = field(default_factory=datetime.utcnow)
    notes: str = ""
    
    @property
    def is_rtk_fixed(self) -> bool:
        """True if this point was collected with a full RTK fixed solution."""
        return self.fix_quality == 4
    
    def is_rtk_float(self) -> bool:
        """True if this point was collected with an RTK float solution."""
        return self.fix_quality == 5