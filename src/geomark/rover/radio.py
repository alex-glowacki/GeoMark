"""Rover radio interface - SiK radio RX for incoming RTCM3 corrections."""

from __future__ import annotations

import serial


class RoverRadio:
    """Wraps the SiK telemetry radio serial port for RTCM3 reception."""
    
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
            
    def read(self, size: int = 1024) -> bytes:
        """Read raw bytes from the radio RX buffer."""
        if not self._serial or not self._serial.is_open:
            raise RuntimeError("Radio serial port is not open")
        return self._serial.read(size)