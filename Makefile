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
