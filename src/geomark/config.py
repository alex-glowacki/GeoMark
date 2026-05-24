"""GeoMark configuration loader and validator."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import yaml

DEFAULT_CONFIG_DIR = Path(__file__).parent.parent.parent / "config"


def load_config(mode: str, config_path: str | None = None) -> dict[str, Any]:
    """Load and return the YAML config for the given mode.
    
    Args:
        mode: Either 'base' or 'rover'.
        config_path: Optional explicit path to a config file.
                     Falls back to config/<mode>.yaml if not provided.
    
    Returns:
        Parsed config as a dictionary.
        
    Raises:
        FileNotFoundError: If the config file does not exist.
        ValueError: If the config is missing required keys.
    """
    if config_path:
        path = Path(config_path)
    else:
        path = DEFAULT_CONFIG_DIR / f"{mode}.yaml"
        
    if not path.exists():
        raise FileNotFoundError(f"Config file not found: {path}")
    
    with open(path) as f:
        config: dict[str, Any] = yaml.safe_load(f)
        
    _validate_config(config, mode)
    return config


def _validate_config(config: dict[str, Any], mode: str) -> None:
    """Raise ValueError if required config keys are missing."""
    required = ["serial", "radio"]
    missing = [key for key in required if key not in config]
    if missing:
        raise ValueError(f"Config missing required sections: {missing}")