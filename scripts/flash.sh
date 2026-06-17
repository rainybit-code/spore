#!/usr/bin/env bash
# Build + flash over USB DFU (no programmer needed).
# Put the device in the bootloader first: hold BOOT, tap RESET
# (or hold BOTH footswitches for 2s to trigger the DFU reboot gesture).
set -euo pipefail
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")/.."
echo "Building..."
make
echo
echo "Put the Daisy in DFU mode (hold BOOT, tap RESET), then it will flash."
make program-dfu
