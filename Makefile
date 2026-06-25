# ============================================================================
#  Daisy Seed + Hothouse synth -- project Makefile
#  Thin wrapper over libDaisy's core Makefile. Scripts in scripts/ call this.
# ============================================================================

# Binary name (build/daisy_synth.bin)
TARGET = daisy_synth

# Needed for MoogLadder / ReverbSc etc.
USE_DAISYSP_LGPL = 1

# Sources: app entry + vendored Hothouse hardware proxy.
# (All mode/io/mod code is header-only and pulled in via main.cpp.)
CPP_SOURCES = src/main.cpp src/hothouse.cpp

# USB identity override (enumerate as "Spore"): a copy of libDaisy's usbd_desc.c.
# Linked before -ldaisy so its descriptor symbols win over the archive's copy. See
# the header note in the file for how to set a real VID/PID before shipping a product.
C_SOURCES += src/usb_identity.c

# Header search root so "config/...", "io/...", "modes/...", "hothouse.h" resolve.
C_INCLUDES = -Isrc

# Vendored libraries (git submodules under lib/).
LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR  = lib/DaisySP

# Run the app from SRAM via the Daisy bootloader instead of the 128 KB internal
# flash -- the firmware outgrew internal flash, and SRAM gives hundreds of KB of
# headroom. The app binary is flashed to QSPI (the bootloader copies it to SRAM at
# boot). One-time: install the bootloader with `make program-boot`; then flash the
# app with `make program-dfu` (or drag the .bin into the Daisy web programmer's
# bootloader slot). Presets live high in QSPI, clear of the app (see io/presets.h).
APP_TYPE = BOOT_SRAM

# Core location and the generic Daisy Makefile (defines: all, clean,
# program-dfu, program-boot, etc.).
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Extra float optimization for the audio DSP hot paths. -ffast-math relaxes IEEE
# strictness (reassociation, reciprocals, no math-errno, fast fp-contract), which
# the per-sample synth/granular/reverb code benefits from. -fno-finite-math-only
# keeps NaN/Inf handling intact -- the Lorenz chaos NaN-guard and the CPU meter
# rely on isnan/isfinite. Appended to CFLAGS (CPPFLAGS = $(CFLAGS), so C++ inherits
# it); deliberately NOT on ASFLAGS. Denormals are already flushed via FPSCR.FZ.
CFLAGS += -ffast-math -fno-finite-math-only

# Link-time optimization. The app is effectively one big translation unit
# (main.cpp pulls in all the header-only DSP), so the win is modest, but it trims
# code size and lets cross-object inlining reach hothouse.cpp. Needs the flag at
# compile and link.
CFLAGS  += -flto
LDFLAGS += -flto

# Keep the USB descriptor override out of LTO: usb_identity.c wins over libDaisy's
# archive copy purely by link order, so compile it as a plain object to keep that
# symbol resolution deterministic (-fno-lto wins as the last flag).
$(BUILD_DIR)/usb_identity.o: CFLAGS += -fno-lto
