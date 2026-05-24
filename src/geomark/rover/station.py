"""Rover station - monitors RTK fix status and collects survey points."""

from __future__ import annotations

from geomark.config import load_config
from geomark.gnss.um980 import UM980


class RoverStation:
    """Receives RTCM3 corrections, monitors RTK fix, and drives the data collector."""
    
    def __init__(self, config_path: str | None = None) -> None:
        self.config = load_config("rover", config_path)
        self.gnss = UM980(
            port=self.config["serial"]["port"],
            baudrate=self.config["serial"]["baudrate"],
        )
        
    def run(self) -> None:
        """Start the rover station - blocking."""
        self.gnss.open()
        self.gnss.configure_rover()
        # TODO: read NMEA from UM980, inject RTCM3 from radio RX, feed collector