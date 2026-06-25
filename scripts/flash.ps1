# Build + flash the app to the Daisy over USB DFU.
# Spore runs from SRAM via the Daisy bootloader: install it once with
# scripts\install-bootloader.ps1, then reset the Daisy so the bootloader's DFU
# window opens (the LED pulses for ~2s) and run this. The app is written to QSPI.
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "env.ps1")
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  Write-Host "Building..." -ForegroundColor Cyan
  make
  Write-Host ""
  Write-Host "Reset the Daisy into the bootloader DFU window (LED pulsing), then it will flash." -ForegroundColor Yellow
  make program-dfu
} finally { Pop-Location }
