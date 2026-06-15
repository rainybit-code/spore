# Build the firmware -> build/daisy_synth.bin
# Requires the Daisy ARM toolchain (arm-none-eabi-gcc + make) on PATH.
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "env.ps1")
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try { make @args } finally { Pop-Location }
