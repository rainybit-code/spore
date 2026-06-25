#!/usr/bin/env bash
# One-shot bootstrap: fetch submodules and build the vendored libraries, so a
# fresh clone is ready for `scripts/build.sh`. Re-run after a submodule update.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
echo "Updating submodules (libDaisy + DaisySP)..."
git -C "$root" submodule update --init --recursive
"$root/scripts/build-libs.sh"
echo "Setup complete. Build the firmware with scripts/build.sh"
