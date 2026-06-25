<#
.SYNOPSIS
    Deploy GeoMark binary to base and/or rover Pi units.
.PARAMETER Target
    Which unit to deploy to: 'base', 'rover', or 'all' (default).
.PARAMETER InstallService
    If set, installs and enables the systemd service on the target Pi.
.EXAMPLE
    .\deploy.ps1
    .\deploy.ps1 -Target rover
    .\deploy.ps1 -Target all -InstallService
#>

param(
    [ValidateSet("base", "rover", "all")]
    [string]$Target = "all",
    [switch]$InstallService
)

$BASE_HOST         = "geomark-base.local"
$ROVER_HOST        = "geomark-rover.local"
$REMOTE_USER       = "pi"
$BINARY_PATH_BASE  = "build/pi-base/geomark"
$BINARY_PATH_ROVER = "build/pi-rover/geomark"
$REMOTE_BIN        = "/usr/local/bin/geomark"

function Deploy-Unit {
    param(
        [string]$Hostname,
        [string]$BinaryPath,
        [string]$ServiceFile
    )

    Write-Host "==> Deploying to $Hostname" -ForegroundColor Cyan

    if (-not (Test-Path $BinaryPath)) {
        Write-Error "Binary not found: $BinaryPath — run cmake build first."
        return
    }

    scp $BinaryPath "${REMOTE_USER}@${Hostname}:/tmp/geomark"
    ssh "${REMOTE_USER}@${Hostname}" "sudo mv /tmp/geomark $REMOTE_BIN && sudo chmod +x $REMOTE_BIN"

    if ($InstallService) {
        scp $ServiceFile "${REMOTE_USER}@${Hostname}:/tmp/geomark.service"
        ssh "${REMOTE_USER}@${Hostname}" "sudo mv /tmp/geomark.service /etc/systemd/system/geomark.service && sudo systemctl daemon-reload && sudo systemctl enable geomark && sudo systemctl restart geomark"
    } else {
        ssh "${REMOTE_USER}@${Hostname}" "sudo systemctl restart geomark 2>/dev/null || true"
    }

    Write-Host "==> Done: $Hostname" -ForegroundColor Green
}

if ($Target -eq "base" -or $Target -eq "all") {
    Deploy-Unit -Hostname $BASE_HOST -BinaryPath $BINARY_PATH_BASE -ServiceFile "deploy/base/systemd/geomark-base.service"
}

if ($Target -eq "rover" -or $Target -eq "all") {
    Deploy-Unit -Hostname $ROVER_HOST -BinaryPath $BINARY_PATH_ROVER -ServiceFile "deploy/rover/systemd/geomark-rover.service"
}