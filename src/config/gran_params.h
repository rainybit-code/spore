// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  gran_params.h  --  Granular-engine controls beyond the 6 physical knobs.
// =============================================================================
//  Normalized 0..1, set over USB MIDI (CC 94.. — see params::midi::kCcGranBase)
//  from the Propagator GRANULAR pod. Defaults reproduce the stock granular sound
//  (so an un-touched pod changes nothing). Defined once in main.cpp.
// =============================================================================
#pragma once

namespace synthbox {

enum GranParam {
  GR_REVERSE = 0,  // probability a grain plays backwards (0..1)
  GR_WIDTH,        // stereo width: per-grain random pan spread (0 mono .. 1 wide)
  GR_SHAPE,        // grain window: 0 soft (Hann) -> 1 hard (flat-top gate)
  GR_SCALE,        // pitch scale-lock: off / major / minor / pentatonic
  GR_COUNT,
};

struct GranParams {
  float v[GR_COUNT] = {0.30f, 0.0f, 0.0f, 0.0f};  // 30% reverse, mono, soft, chromatic
};

extern GranParams g_granParams;  // defined in main.cpp

}  // namespace synthbox
