"""Basic tests for the geomark package."""

import geomark


def test_version() -> None:
    assert geomark.__version__ == "0.1.0"