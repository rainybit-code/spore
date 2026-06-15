# Open the Daisy USB serial log (for daisy.PrintLine debug output).
# The Daisy shows up as a USB CDC serial port. This finds the newest COM port
# and opens it with PuTTY if available, else prints guidance.
$ErrorActionPreference = "Stop"
$baud = 115200

$ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
if (-not $ports) {
  Write-Host "No COM ports found. Is the Daisy connected and running (not in DFU)?" -ForegroundColor Yellow
  exit 1
}
$port = $ports[-1]
Write-Host "Using $port @ $baud. (Available: $($ports -join ', '))" -ForegroundColor Cyan

$putty = Get-Command putty.exe -ErrorAction SilentlyContinue
if ($putty) {
  & $putty.Source -serial $port -sercfg "$baud,8,n,1,N"
} else {
  Write-Host "PuTTY not found. Open $port at $baud in your serial terminal," -ForegroundColor Yellow
  Write-Host "or 'pip install pyserial' then: python -m serial.tools.miniterm $port $baud"
}
