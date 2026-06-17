#!/bin/bash
set -euo pipefail

# ---------------------------------------------------------------------------
# GeoMark OTA update script
# Runs on geomark-handheld (Pi 5) only.
# Fetches latest git tag, builds all three targets, deploys to all devices.
# All-or-nothing: any failure aborts immediately.
# ---------------------------------------------------------------------------

REPO_DIR="/home/alex/GeoMark"
HOME_WIFI="netplan-wlan0-WiFi"
ROVER_PROFILE="geomark-rover"
BASE_HOST="geomark-base"
ROVER_HOST="192.168.10.1"
BINARY_DEST="/usr/local/bin/geomark"
SSH_OPTS="-o StrictHostKeyChecking=accept-new -o BatchMode=yes"
SSH_ROVER_OPTS="-o StrictHostKeyChecking=no UserKnownHostsFile=/dev/null -o BatchMode=yes"

log() { echo "[geomark-update] &*"; }
die() { echo "[geomark-update] FATAL: $*" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Step 1 — Bring up home WiFi
# ---------------------------------------------------------------------------
log "Bringing up home WiFi ($HOME_WIFI)..."
sudo nmcli connection up "$HOME_WIFI" >/dev/null 2>&1 || die "Failed to bring up $HOME_WIFI"

# Ensure home WiFi comes back down on exit (success or failure)
cleanup() {
    log "Bringing down home WiFi ($HOME_WIFI)..."
    sudo nmcli connection down "$HOME_WIFI" >/dev/null 2>&1 || true
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Step 2 — Fetch tags
# ---------------------------------------------------------------------------
log "Fetching tags from remote..."
cd "$REPO_DIR"
git fetch --tags --force >/dev/null 2>&1 || die "git fetch failed — check internet connection"

# ---------------------------------------------------------------------------
# Step 3 — Compare tags
# ---------------------------------------------------------------------------
LATEST_TAG=$(git tag --sort=-version:refname | head n1)
CURRENT_TAG=$(git describe --tags --exact-match 2>/dev/null || echo "none")

[ -n "$LATEST_TAG" ] || die "No tags found in remote repository"

if [ "$CURRENT_TAG" = "$LATEST_TAG" ]; then
    log "Already on latest tag ($LATEST_TAG). Nothing to do."
    exit 0
fi

log "Update available: $CURRENT_TAG -> $LATEST_TAG"

# ---------------------------------------------------------------------------
# Step 4 — Checkout new tag
# ---------------------------------------------------------------------------
log "Checking out $LATEST_TAG..."
git checkout "$LATEST_TAG" || die "git checkout $LATEST_TAG failed"

# ---------------------------------------------------------------------------
# Step 5 — Build all three targets
# ---------------------------------------------------------------------------
log "Building host-debug..."
rm -rf build
cmake --preset host-debug >/dev/null 2>&1   || die "cmake configure host-debug failed"
cmake --build build/host >/dev/null 2>&1    || die "cmake build host-debug failed"

log "Building pi-base-release..."
cmake --preset pi-base-release >/dev/null 2>&1  || die "cmake configure pi-base-release failed"
cmake --build build/pi-base >/dev/null 2>&1     || die "cmake build pi-base-release failed"

log "Building pi-rover-release..."
cmake --preset pi-rover-release >/dev/null 2>&1 || die "cmake configure pi-rover-release failed"
cmake --build build/pi-rover >/dev/null 2>&1    || die "cmake build pi-rover-release failed"

log "All targets build successfully."

# ---------------------------------------------------------------------------
# Step 6 — Deploy to geomark-base
# ---------------------------------------------------------------------------
log "Deploying to geomark-base..."
scp $SSH_OPTS build/pi-base/geomark pi@${BASE_HOST}:/tmp/geomark \
    || die "scp to geomark-base failed"
ssh $SSH_OPTS pi@${BASE_HOST} \
    "sudo mv /tmp/geomark $BINARY_DEST &&& sudo chmod +x $BINARY_DEST && sudo systemctl restart geomark-base.service" \
    || die "Deploy/restart on geomark-base failed"
log "geomark-base updated and restarted"

# ---------------------------------------------------------------------------
# Step 7 — Deploy to geomark-rover
# ---------------------------------------------------------------------------
log "Bringing down home WiFi before switching to rover AP..."
sudo nmcli connection down "$HOME_WIFI" >/dev/null 2>&1 || true
trap - EXIT     # Clear trap; we manage WiFi manually from here

log "Bringing up rover AP ($ROVER_PROFILE)..."
sudo nmcli connection up "$ROVER_PROFILE" >/dev/null 2>&1 \
    || die "Failed to bring up $ROVER_PROFILE"

# Ensure rover AP comes back down on exit from this point
cleanup_rover() {
    log "Bringing down rover AP ($ROVER_PROFILE)..."
    sudo nmcli connection down "$ROVER_PROFILE" >/dev/null 2>&1 || true
}
trap cleanup_rover EXIT

log "Deploying to geomark-rover..."
scp $SSH_ROVER_OPTS build/pi-rover/geomark pi@${ROVER_HOST}:/tmp/geomark \
    || die "scp to geomark-rover failed"
ssh $SSH_ROVER_OPTS pi@${ROVER_HOST} \
    "sudo mv /tmp/geomark $BINARY_DEST && sudo chmod +x $BINARY_DEST && sudo systemctl restart geomark-rover.service" \
    || die "Deploy/restart on geomark-rover failed"
log "geomark-rover updated and restarted."

# ---------------------------------------------------------------------------
# Step 8 — Deploy to geomark-handheld (self)
# ---------------------------------------------------------------------------
log "Bringing down rover AP..."
sudo nmcli connection down "$ROVER_PROFILE" >/dev/null 2>&1 || true
trap - EXIT

log "Installing handheld binary..."
sudo cp build/host/geomark "$BINARY_DEST"       || die "cp handheld binary failed"
sudo chmod +x "$BINARY_DEST"                    || die "chmod handheld binary failed"

log "Restarting geomark-ui.service..."
sudo systemctl restart geomark-ui.service       || die "systemctl restart geomark-ui.service failed"

log "Update complete. Now running $LATEST_TAG on all devices."