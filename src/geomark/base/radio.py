"""Base station radio interface - SiK radio TX for RTCM3 correction broadcast."""

from __future__ import annotations

import serial


class BaseRadio:
    """Wraps the SiK telemetry radio serial port for RTMC3 transmission."""
    
    def __init__(self, port: str, baudrate: int = 57600) -> None:
        self.port = port
        self.baudrate = baudrate
        self._serial: serial.Serial | None = None
        
    def open(self) -> None:
        """Open the radio serial port."""
        self._serial = serial.Serial(self.port, self.baudrate, timeout=1)
        
    def close(self) -> None:
        """Close the radio serial port."""
        if self._serial and self._serial.is_open:
            self._serial.close()
            
    def send(self, data: bytes) -> None:
        """Writes raw bytes to the radio TX."""
        if not self._serial or not self._serial.is_open:
            raise RuntimeError("Radio serial port is not open")
        self._serial.write(data)