#!/usr/bin/env bash
# GeoMark - Base station startup script
# Called by geomark-base.service on boot.

set - euo pipefail

VENV_PATH="/home/pi/geomark/.venv"
CONFIG_PATH="/home/pi/geomark/config/base.yaml"

source "${VENV_PATH}/bin/activate"
exec geomark base --config "${CONFIG_PATH}"