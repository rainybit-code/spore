// =============================================================================
//  synth_params.h  --  extended synth voice parameters (beyond the 6 knobs).
// =============================================================================
//  Normalized 0..1, set over USB MIDI (CC 40.. — see params::midi::kCcSynthBase)
//  from the Propagator synth panel. Stored as a flat array so MIDI can index it
//  and the voice can read it by name. Defined once in main.cpp.
// =============================================================================
#pragma once

namespace synthbox {

enum SynthParam {
  SP_DETUNE = 0,   // oscillator spread
  SP_SUB,          // sub-oscillator level
  SP_SUSTAIN,      // amp ADSR sustain level
  SP_RELEASE,      // amp ADSR release time
  SP_FENV_AMT,     // filter-envelope -> cutoff amount
  SP_FENV_TIME,    // filter-envelope decay time
  SP_GLIDE,        // portamento time
  SP_SPREAD,       // stereo width
  SP_WAVE,         // oscillator waveform (0..1 -> Sin / Tri / Saw / Sqr)
  SP_COUNT,
};

struct SynthParams {
  float v[SP_COUNT] = {0.25f, 0.40f, 0.70f, 0.30f, 0.50f, 0.30f, 0.00f, 0.60f, 0.66f};
};

extern SynthParams g_synthParams;  // defined in main.cpp

}  // namespace synthbox
