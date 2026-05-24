"""Survey session manager - tracks collected points for a single field session."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Iterator  # noqa: UP035

from geomark.collector.point import SurveyPoint


class SurveySession:
    """Manages a single survey session - a named collection of survey points.

    Attributes:
        notes: Human-readable session identifier (e.g. 'Site-A-2026-05-22').
        started_at: UTC timestamp when the session was created.
        points: Ordered list of collected survey points.
    """

    def __init__(self, name: str | None = None) -> None:
        self.name = name or datetime.now(timezone.utc).strftime("session_%Y%m%d_%H%M%S")  # noqa: UP017
        self.started_at: datetime = datetime.now(timezone.utc)  # noqa: UP017
        self.points: list[SurveyPoint] = []

    def add_point(self, point: SurveyPoint) -> None:
        """Append a survey point to this session."""
        self.points.append(point)

    def remove_last(self) -> SurveyPoint | None:
        """Remove and return the most recently added point, or None if empty."""
        return self.points.pop() if self.points else None

    def __len__(self) -> int:
        return len(self.points)

    def __iter__(self) -> Iterator[SurveyPoint]:
        return iter(self.points)
