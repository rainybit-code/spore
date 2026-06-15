# Prepend the project-local portable toolchain to PATH (if present).
# Source this from the other scripts: . "$(dirname "$0")/env.sh"
# Falls back silently to system PATH if the local toolchain folder isn't there.
_envroot="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)"
_tc="$_envroot/toolchain"
if [ -d "$_tc" ]; then
  _arm=$(ls -d "$_tc"/xpack-arm-none-eabi-gcc-*/bin 2>/dev/null | head -1)
  _bt=$(ls -d "$_tc"/xpack-windows-build-tools-*/bin 2>/dev/null | head -1)
  [ -n "$_arm" ] && export PATH="$_arm:$PATH"
  [ -n "$_bt" ]  && export PATH="$_bt:$PATH"
  [ -d "$_tc/dfu-util/bin" ] && export PATH="$_tc/dfu-util/bin:$PATH"
fi
