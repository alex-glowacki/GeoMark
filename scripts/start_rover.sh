#!/usr/bin/env bash
# GeoMark - Rover station startup script
# Called by geomark-rover.service on boot.

set -euo pipefail

VENV_PATH="/home/pi/geomark/.venv"
CONFIG_PATH="/home/pi/geomark/config/rover.yaml"

source "${VENV_PATH}/bin/activate"
exec geomark rover --config "${CONFIG_PATH}"