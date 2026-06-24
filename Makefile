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

# Core location and the generic Daisy Makefile (defines: all, clean,
# program-dfu, program, etc.).
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Extra float optimization for the audio DSP hot paths. -ffast-math relaxes IEEE
# strictness (reassociation, reciprocals, no math-errno, fast fp-contract), which
# the per-sample synth/granular/reverb code benefits from. -fno-finite-math-only
# keeps NaN/Inf handling intact -- the Lorenz chaos NaN-guard and the CPU meter
# rely on isnan/isfinite. Appended to CFLAGS (CPPFLAGS = $(CFLAGS), so C++ inherits
# it); deliberately NOT on ASFLAGS. Denormals are already flushed via FPSCR.FZ.
CFLAGS += -ffast-math -fno-finite-math-only
