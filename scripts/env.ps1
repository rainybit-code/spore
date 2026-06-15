# Prepend the project-local portable toolchain to PATH for this session (if present).
# Dot-sourced by the other scripts. Falls back silently to system PATH if the
# local toolchain folder isn't there.
$envRoot = Split-Path $PSScriptRoot -Parent
$tc = Join-Path $envRoot "toolchain"
if (Test-Path $tc) {
  $arm = Get-ChildItem -Path $tc -Directory -Filter "xpack-arm-none-eabi-gcc-*" -ErrorAction SilentlyContinue | Select-Object -First 1
  $bt  = Get-ChildItem -Path $tc -Directory -Filter "xpack-windows-build-tools-*" -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($arm) { $env:PATH = (Join-Path $arm.FullName "bin") + ";" + $env:PATH }
  if ($bt)  { $env:PATH = (Join-Path $bt.FullName  "bin") + ";" + $env:PATH }
  $dfu = Join-Path $tc "dfu-util\bin"
  if (Test-Path $dfu) { $env:PATH = $dfu + ";" + $env:PATH }
}
