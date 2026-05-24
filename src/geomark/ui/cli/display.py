"""Rich-based terminal display - CLI fallback for field use without a browser."""

from __future__ import annotations

from rich.console import Console
from rich.table import Table
from rich.live import Live

from geomark.gnss.nmea import GNSSFix
from geomark.collector.session import SurveySession

console = Console()


def render_fix_status(fix: GNSSFix) -> None:
    """Print the current RTK fix status to the terminal."""
    quality_labels = {
        0: "[red]No Fix[/red]",
        1: "[yellow]GPS[/yellow]",
        2: "[yellow]DGPS[/yellow]",
        4: "[green]RTK Fixed[/green]",
        5: "[cyan]RTK Float[/cyan]",
    }
    label = quality_labels.get(fix.fix_quality, "[white]Unknown[/white]")
    console.print(
        f"Fix: {label} | "
        f"Lat: {fix.latitude:.8f} | "
        f"Lon: {fix.longitude:.8f} | "
        f"Alt: {fix.altitude:.3f}m | "
        f"Sats: {fix.satellites} | "
        f"HDOP: {fix.hdop:.2f}"
    )
    
    
def render_session_table(session: SurveySession) -> Table:
    """Build a Rich table of all collected points in the current session."""
    table = Table(title=f"Session: {session.name}", show_lines=True)
    table.add_column("Name", style="bold")
    table.add_column("Latitude", justify="right")
    table.add_column("Longitude", justify="right")
    table.add_column("Altitude", justify="right")
    table.add_column("Fix", justify="center")
    table.add_column("HDOP", justify="right")
    
    for point in session:
        fix_label = "[green]Fixed[/green]" if point.is_rtk_fixed else "[cyan]Float[/cyan]"
        table.add_row(
            point.name,
            f"{point.longitude:.8f}",
            f"{point.longitude:.8f}",
            f"{point.altitude:.3f}m",
            fix_label,
            f"{point.hdop:.2f}",
        )
        
    return table