# Build + flash over USB DFU (no programmer needed).
# Put the pedal in bootloader first: hold BOOT, tap RESET
# (or hold BOTH footswitches for 2s to trigger the DFU reboot gesture).
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "env.ps1")
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  Write-Host "Building..." -ForegroundColor Cyan
  make
  Write-Host ""
  Write-Host "Put the Daisy in DFU mode (hold BOOT, tap RESET), then it will flash." -ForegroundColor Yellow
  make program-dfu
} finally { Pop-Location }
