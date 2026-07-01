#!/bin/bash

PROFILE="geomark-rover"
ROVER_IP="192.168.10.1"
TIMEOUT=60
INTERVAL=2

elapsed=0
while true; do
    if nmcli connection up "$PROFILE" 2>/dev/null; then
        echo "Connected to $PROFILE"
        break
    fi

    if (( elapsed >= TIMEOUT )); then
        echo "Timeout waiting to connect to $PROFILE after ${TIMEOUT}s" >&2
        exit 1
    fi

    sleep "$INTERVAL"
    ((elapsed += INTERVAL))
done

# Wait for rover to be reachable
elapsed=0
until ping -c1 -W1 "$ROVER_IP" >/dev/null 2>&1; do
    if (( elapsed >= TIMEOUT )); then
        echo "Timeout waiting for $ROVER_IP" >&2
        exit 1
    fi
    sleep "$INTERVAL"
    ((elapsed += INTERVAL))
done

echo "Rover reachable at $ROVER_IP"
exit 0