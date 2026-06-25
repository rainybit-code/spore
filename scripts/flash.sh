#!/usr/bin/env bash
# Build + flash over USB DFU (no programmer needed).
# Put the device in the bootloader first: hold BOOT, tap RESET
# (or send MIDI CC 119 >= 64 from the editor to reboot into DFU).
set -euo pipefail
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")/.."
echo "Building..."
make
echo
echo "Put the Daisy in DFU mode (hold BOOT, tap RESET), then it will flash."
make program-dfu
