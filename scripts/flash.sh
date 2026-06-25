#!/usr/bin/env bash
# Build + flash the app to the Daisy over USB DFU.
# Spore runs from SRAM via the Daisy bootloader: install it once with
# scripts/install-bootloader.sh, then reset the Daisy so the bootloader's DFU
# window opens (the LED pulses for ~2s) and run this. The app is written to QSPI.
set -euo pipefail
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")/.."
echo "Building..."
make
echo
echo "Reset the Daisy into the bootloader DFU window (LED pulsing), then it will flash."
make program-dfu
