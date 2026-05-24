"""UM980 UART driver - command interface and serial stream management."""

from __future__ import annotations

import serial


class UM980:
    """Low-level driver for the Unicorecomm UM98- GNSS receiver.
    
    Handles serial port lifecycle, command transmission, and raw data reading.
    Both base and rover stations share this driver.
    """
    
    def __init__(self, port: str, baudrate: int = 115200) -> None:
        self.port = port
        self.baudrate = baudrate
        self._serial: serial.Serial | None = None
        
    def open(self) -> None:
        """Open the UART connection to the UM980."""
        self._serial = serial.Serial(
            self.port,
            self.baudrate,
            timeout=1,
        )
        
    def close(self) -> None:
        """Close the UART connection."""
        if self._serial and self._serial.is_open:
            self._serial.close()
            
    def send_command(self, command: str) -> None:
        """Send a UART command string to the UM980.
        
        Args:
            command: A Unicorecomm ASCII command (e.g. 'MODE ROVER').
        """
        if not self._serial or not self._serial.is_open:
            raise RuntimeError("UM980 serial port is not open")
        self._serial.write(f"{command}\r\n".encode())
        
    def read_line(self) -> str:
        """Read one line from the UM980 serial output.
        
        Returns:
            A decoded ASCII line (NMEA sentence or response string).
        """
        if not self._serial or not self._serial.is_open:
            raise RuntimeError("UM980 serial port is not open")
        return self._serial.readline().decode(errors="replace").strip()
    
    def configure_base(self, survey_in_duration: int = 60, survey_in_accuracy: float = 0.05) -> None:
        """Configure the UM98- for base station (survey-in) mode.
        
        Args:
            survey_in_duration: Minimum survey-in time in seconds.
            survey_in_accuracy: Required position accuracy in meters before lock.
        """
        self.send_command("MODE BASE TIME 60 1.5")  # TODO: parameterize from args
        self.send_command("RTCM1005 COM1 1")
        self.send_command("RTCM1033 COM1 1")
        self.send_command("RTCM1074 COM1 1")
        self.send_command("RTCM1084 COM1 1")
        self.send_command("RTCM1094 COM1 1")
        self.send_command("RTCM1124 COM1 1")
        
    def configure_rover(self) -> None:
        """Configure the UM980 for rover (RTK) mode."""
        self.send_command("MODE ROVER")
        self.send_command("GPGGA COM1 0.1")
        self.send_command("GPGSV COM1 1")