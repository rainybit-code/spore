#!/usr/bin/env bash
# Open the Daisy USB serial log (for daisy.PrintLine debug output).
# Uses pyserial's miniterm if available.
set -euo pipefail
BAUD="${2:-115200}"
PORT="${1:-}"

if [ -z "$PORT" ]; then
  # Best-effort autodetect (Linux/macOS).
  PORT=$(ls /dev/tty.usbmodem* /dev/ttyACM* 2>/dev/null | head -n1 || true)
fi
if [ -z "$PORT" ]; then
  echo "No serial port found. Pass it explicitly: scripts/monitor.sh /dev/ttyACM0"
  exit 1
fi
echo "Opening $PORT @ $BAUD ..."
python3 -m serial.tools.miniterm "$PORT" "$BAUD"
