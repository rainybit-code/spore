#!/usr/bin/env bash
# One-time (or after submodule update) build of libDaisy + DaisySP.
set -euo pipefail
. "$(dirname "$0")/env.sh"
root="$(cd "$(dirname "$0")/.." && pwd)"
echo "Building libDaisy..."
make -C "$root/lib/libDaisy" -j
echo "Building DaisySP..."
make -C "$root/lib/DaisySP" -j
echo "Libraries built."
