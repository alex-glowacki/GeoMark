"""Base station - manages survey-in mode and RTCM3 correction broadcast."""

from __future__ import annotations

from geomark.config import load_config
from geomark.gnss.um980 import UM980


class BaseStation:
    """Operates the UM980 in survey-in mode and broadcasts RTCM3 over the radio link."""
    
    def __init__(self, config_path: str | None = None) -> None:
        self.config = load_config("base", config_path)
        self.gnss = UM980(
            port=self.config["serial"]["port"],
            baudrate=self.config["serial"]["baudrate"],
        )
        
    def run(self) -> None:
        """Start the base station - blocking."""
        self.gnss.open()
        self.gnss.configure_base(
            survey_in_duration=self.config.get("survey_in_duration", 60),
            survey_in_accuracy=self.config.get("survey_in_accuracy", 0.05),
        )
        # TODO: pipe RTCM3 output to radio TX