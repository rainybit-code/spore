// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  controls.h  --  thin helpers over the Hothouse control surface.
// =============================================================================
//  Keeps switch/footswitch semantics in one readable place so modes and main()
//  don't repeat enum plumbing.
// =============================================================================
#pragma once

#include "hothouse.h"

namespace synthbox {

using clevelandmusicco::Hothouse;

enum Mode {
  MODE_SYNTH = 0,       // TOGGLE 1 UP
  MODE_GRANULAR = 1,    // TOGGLE 1 MIDDLE
  MODE_GENERATIVE = 2,  // TOGGLE 1 DOWN
  MODE_COUNT = 3,
};

// Toggle 1 selects the mode. NOTE: use a 3-position (ON-OFF-ON) switch here so
// all three modes are reachable; a 2-position switch can't hit MIDDLE.
inline Mode CurrentMode(Hothouse& hw) {
  switch (hw.GetToggleswitchPosition(Hothouse::TOGGLESWITCH_1)) {
    case Hothouse::TOGGLESWITCH_UP:     return MODE_SYNTH;
    case Hothouse::TOGGLESWITCH_MIDDLE: return MODE_GRANULAR;
    case Hothouse::TOGGLESWITCH_DOWN:   return MODE_GENERATIVE;
    default:                            return MODE_SYNTH;
  }
}

// Rising-edge (press) detection for the footswitches.
inline bool Footswitch1Pressed(Hothouse& hw) {
  return hw.switches[Hothouse::FOOTSWITCH_1].RisingEdge();
}
inline bool Footswitch2Pressed(Hothouse& hw) {
  return hw.switches[Hothouse::FOOTSWITCH_2].RisingEdge();
}

// Convenience: read a 3-position toggle as an int (0/1/2).
inline int TogglePos(Hothouse& hw, Hothouse::Toggleswitch tsw) {
  auto p = hw.GetToggleswitchPosition(tsw);
  return (p == Hothouse::TOGGLESWITCH_UP)     ? 0
       : (p == Hothouse::TOGGLESWITCH_MIDDLE) ? 1
                                              : 2;
}

}  // namespace synthbox
