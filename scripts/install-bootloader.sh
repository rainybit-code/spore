#!/usr/bin/env bash
# One-time: install the Daisy bootloader into the Seed's internal flash, so Spore
# (which runs from SRAM) can be loaded. Put the Daisy in DFU mode first: hold
# BOOT, tap RESET. After this, use scripts/flash.sh to load the app.
set -euo pipefail
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")/.."
echo "Put the Daisy in DFU mode (hold BOOT, tap RESET), then installing the bootloader..."
make program-boot
echo "Bootloader installed. Flash the app with scripts/flash.sh."
