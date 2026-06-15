#!/usr/bin/env bash
# Build the firmware -> build/daisy_synth.bin
# Requires the Daisy ARM toolchain (arm-none-eabi-gcc + make) on PATH.
set -euo pipefail
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")/.."
make "$@"
