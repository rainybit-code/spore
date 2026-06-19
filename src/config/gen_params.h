// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  gen_params.h  --  Generative-mode steering parameters (beyond the 6 knobs).
// =============================================================================
//  Normalized 0..1, set over USB MIDI (CC 32.. — see params::midi::kCcGenBase)
//  from the Propagator GENERATIVE pod. The timbre still rolls RANDOMLY on re-seed;
//  these only bias the roll (Brightness/Texture) and steer behaviour (Chord/Swell/
//  Motion) + the morph depth (Wander). Defined once in main.cpp.
// =============================================================================
#pragma once

namespace synthbox {

enum GenParam {
  GP_CHORD = 0,   // note stacking: single notes -> triads
  GP_SWELL,       // note length: short stabs -> long evolving pads (atk/rel scale)
  GP_MOTION,      // random-walk step size (gentle drift -> wide leaps)
  GP_BRIGHT,      // timbre brightness bias (weights the rolled scan + live cutoff)
  GP_TEXTURE,     // timbre complexity bias (weights rolled FM / fold / drive / unison / filter)
  GP_WANDER,      // how much a rolled timbre keeps morphing over time (scan/cutoff drift)
  GP_COUNT,
};

struct GenParams {
  float v[GP_COUNT] = {0.40f, 0.50f, 0.45f, 0.50f, 0.50f, 0.35f};
};

extern GenParams g_genParams;  // defined in main.cpp

}  // namespace synthbox
