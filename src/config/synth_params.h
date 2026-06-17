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
  SP_LFO_RATE,     // LFO rate
  SP_LFO_DEPTH,    // LFO depth
  SP_LFO_SHAPE,    // LFO shape (0..1 -> Sin / Tri / Saw / Sqr)
  SP_LFO_DEST,     // LFO destination (0..1 -> Off / Vibrato / Filter / Tremolo)
  SP_VOICES,       // polyphony (0..1 -> 1..6 voices; fewer = more CPU headroom)
  // --- digital / wavetable engine (Phase 3) ---
  SP_ENGINE,       // voice engine (0..1 -> Analog / Wavetable)
  SP_WT_POS,       // wavetable scan position (morph sine -> bright)
  SP_FM_AMT,       // FM depth (modulator -> carrier phase)
  SP_FM_RATIO,     // FM ratio (0..1 -> {0.5, 1, 2, 3})
  SP_FOLD,         // wavefold amount (digital grit)
  SP_WT_BANK,      // wavetable bank (0..1 -> Saw / Square / Organ / Vocal / Digital)
  // --- tone shaping (electronic: stabs + gritty bass) ---
  SP_DRIVE,        // pre-filter saturation amount (grit)
  SP_FILTER,       // filter type (0..1 -> Svf clean / MoogLadder fat)
  SP_UNISON,       // unison voices (0..1 -> 1..4 detuned osc, super-saw)
  SP_SUB_OCT,      // sub octave (0..1 -> -1 / -2)
  SP_SUB_WAVE,     // sub waveform (0..1 -> square / sine)
  // --- modulation: LFO2 + 3-slot mod matrix ---
  SP_LFO2_RATE,    // LFO2 rate
  SP_LFO2_SHAPE,   // LFO2 shape (Sin / Tri / Saw / Sqr)
  SP_M1_SRC, SP_M1_DST, SP_M1_AMT,   // slot 1: source / destination / bipolar amount
  SP_M2_SRC, SP_M2_DST, SP_M2_AMT,   // slot 2
  SP_M3_SRC, SP_M3_DST, SP_M3_AMT,   // slot 3
  SP_COUNT,
};

struct SynthParams {
  float v[SP_COUNT] = {0.25f, 0.40f, 0.70f, 0.30f, 0.50f, 0.30f, 0.00f, 0.60f, 0.66f,
                       0.30f, 0.00f, 0.00f, 0.33f, 0.60f,
                       0.00f, 0.30f, 0.00f, 0.25f, 0.00f, 0.00f,
                       0.00f, 0.00f, 0.33f, 0.00f, 0.00f,
                       0.30f, 0.00f,
                       0.00f, 0.00f, 0.50f,
                       0.00f, 0.00f, 0.50f,
                       0.00f, 0.00f, 0.50f};
};

extern SynthParams g_synthParams;  // defined in main.cpp

}  // namespace synthbox
