# One-time (or after submodule update) build of libDaisy + DaisySP.
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "env.ps1")
$root = Split-Path $PSScriptRoot -Parent
Write-Host "Building libDaisy..." -ForegroundColor Cyan
make -C "$root/lib/libDaisy" -j
Write-Host "Building DaisySP..." -ForegroundColor Cyan
make -C "$root/lib/DaisySP" -j
Write-Host "Libraries built." -ForegroundColor Green
