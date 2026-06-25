# One-time: install the Daisy bootloader into the Seed's internal flash, so Spore
# (which runs from SRAM) can be loaded. Put the Daisy in DFU mode first: hold
# BOOT, tap RESET. After this, use scripts\flash.ps1 to load the app.
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "env.ps1")
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
  Write-Host "Put the Daisy in DFU mode (hold BOOT, tap RESET), then installing the bootloader..." -ForegroundColor Yellow
  make program-boot
  Write-Host "Bootloader installed. Flash the app with scripts\flash.ps1." -ForegroundColor Green
} finally { Pop-Location }
