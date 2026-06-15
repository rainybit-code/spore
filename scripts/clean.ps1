# Remove build artifacts for the app (not the libraries).
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "env.ps1")
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try { make clean } finally { Pop-Location }
