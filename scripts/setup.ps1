# One-shot bootstrap: fetch submodules and build the vendored libraries, so a
# fresh clone is ready for scripts\build.ps1. Re-run after a submodule update.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Write-Host "Updating submodules (libDaisy + DaisySP)..." -ForegroundColor Cyan
git -C $root submodule update --init --recursive
. (Join-Path $PSScriptRoot "build-libs.ps1")
Write-Host "Setup complete. Build the firmware with scripts\build.ps1" -ForegroundColor Green
