// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  presets.h  --  preset data model stored in QSPI flash.
// =============================================================================
//  Three presets per mode, indexed by the Toggle-2 position (so the slot IS the
//  variant -- see the gesture in main.cpp). A preset captures the mode's sound:
//  its extended params, the MODE + FX knob layers, FX selection, and the master
//  stage. It does NOT store the variant (that is the slot index) or physical
//  toggle positions.
//
//  The bank lives in QSPI via daisy::PersistentStorage; main.cpp owns the
//  storage instance and the capture/apply glue (it touches the live globals).
// =============================================================================
#pragma once

#include <cstdint>
#include <cstring>

#include "config/synth_params.h"  // SP_COUNT (the largest per-mode param set)
#include "config/gen_params.h"    // GP_COUNT
#include "config/gran_params.h"   // GR_COUNT
#include "io/controls.h"          // MODE_COUNT
#include "io/knobs.h"             // ShiftKnobs::kKnobs

namespace synthbox {

// Bump when the layout below changes; a stored bank with a different version is
// discarded back to empty defaults at boot (presets are convenience, not data).
constexpr uint32_t kPresetVersion = 1;

// QSPI byte offset for the preset bank. With APP_TYPE=BOOT_SRAM the app binary
// lives at the start of QSPI, so the bank sits 4 MB into the 8 MB chip -- well
// clear of the app, and preserved when the app is reflashed.
constexpr uint32_t kPresetQspiOffset = 0x400000;

// Three slots per mode == the three Toggle-2 positions (UP / MIDDLE / DOWN).
constexpr int kPresetSlots = 3;

// Synth has the most params; gen/gran store into the same buffer's prefix.
static_assert(SP_COUNT >= GP_COUNT && SP_COUNT >= GR_COUNT,
              "modeParams[] is sized for the largest mode (synth)");

// One stored preset. Defaults describe an empty slot (used = 0).
struct PresetData {
    uint8_t used = 0;            // 1 once something has been saved here
    uint8_t fxMode = 0;          // GlobalFx::Mode (0 off / 1 delay / 2 reverb)
    uint8_t delaySync = 0;       // delay tempo-sync division (0 free / 1..4)
    uint8_t masterFiltType = 0;  // 0 off / 1 LP / 2 BP / 3 HP
    float masterVol = 1.0f, masterCut = 1.0f, masterRes = 0.0f;
    float modeKnobs[ShiftKnobs::kKnobs] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    float fxKnobs[ShiftKnobs::kKnobs] = {0.30f, 0.40f, 0.35f, 0.70f, 0.60f, 0.70f};
    float modeParams[SP_COUNT] = {0};  // active mode's param array (prefix for gen/gran)
};

// The whole bank: 3 modes x 3 slots. PersistentStorage::Save() compares with
// operator!=, so a (memcmp-based) equality operator is required here.
struct PresetBank {
    uint32_t version = kPresetVersion;
    PresetData slot[MODE_COUNT][kPresetSlots];

    bool operator==(const PresetBank& o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
    bool operator!=(const PresetBank& o) const { return !(*this == o); }
};

}  // namespace synthbox
