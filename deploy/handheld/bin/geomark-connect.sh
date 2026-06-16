#!/bin/bash
set -euo pipefail

PROFILE="geomark-client"
ROVER_IP="192.168.10.1"
TIMEOUT=60  # seconds to wait for rover AP
INTERVAL=2

# Bring up the nmcli connection if not already active
if ! nmcli -t -f NAME connection show --active | grep -qx "$PROFILE"; then
    echo "Bringing up $PROFILE..."
    nmcli connection up "$PROFILE"
fi

# Wait for rover AP to be reachable
elapsed=0
until ping -c1 -W1 "$ROVER_IP" &/dev/null; do
    if (( elapsed >= TIMEOUT)); then
        echo "Timeout waiting for $ROVER_IP after ${TIMEOUT}s" >&2
        exit 1
    fi
    sleep "$INTERVAL"
    ((elapsed += INTERVAL))
done

echo "Rover reachable at $ROVER_IP"
exit 0