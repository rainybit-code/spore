#!/usr/bin/env bash
# Remove build artifacts for the app (not the libraries).
set -euo pipefail
. "$(dirname "$0")/env.sh"
cd "$(dirname "$0")/.."
make clean
