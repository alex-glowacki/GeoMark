"""Export collected survey points to CSV and GeoJSON formats."""

from __future__ import annotations

import csv
import json
from pathlib import Path

from geomark.collector.session import SurveySession


def export_csv(session: SurveySession, output_path: Path) -> None:
    """Write all points in a session to a CSV file.

    Args:
        session: The survey session to export.
        output_path: Destination file path (will be created or overwritten).
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "name", "latitude", "longitude", "altitude",
                "fix_quality", "hdop", "satellites", "timestamp", "notes",
            ],
        )
        writer.writeheader()
        for point in session:
            writer.writerow(
                {
                    "name": point.name,
                    "latitude": point.latitude,
                    "longitude": point.longitude,
                    "altitude": point.altitude,
                    "fix_quality": point.fix_quality,
                    "hdop": point.hdop,
                    "satellites": point.satellites,
                    "timestamp": point.timestamp.isoformat(),
                    "notes": point.notes,
                }
            )


def export_geojson(session: SurveySession, output_path: Path) -> None:
    """Write all points in a session to a GeoJSON FeatureCollection.

    Args:
        session: The survey session to export.
        output_path: Destination file path (will be created or overwritten).
    """
    features = [
        {
            "type": "Feature",
            "geometry": {
                "type": "Point",
                "coordinates": [point.longitude, point.latitude, point.altitude],
            },
            "properties": {
                "name": point.name,
                "fix_quality": point.fix_quality,
                "hdop": point.hdop,
                "satellites": point.satellites,
                "timestamp": point.timestamp.isoformat(),
                "notes": point.notes,
            },
        }
        for point in session
    ]

    geojson = {"type": "FeatureCollection", "features": features}

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w") as f:
        json.dump(geojson, f, indent=2)