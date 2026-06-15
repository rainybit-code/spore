// =============================================================================
//  params.h  --  SINGLE SOURCE OF TRUTH for every tunable value in the synth.
// =============================================================================
//  Anything you might want to tweak, tune, or explore lives here -- grouped by
//  module, with units and sensible ranges in the comments. Change a value,
//  rebuild (`scripts/flash`), and listen. No magic numbers buried in DSP code.
//
//  Two ways to tune:
//    1. Structural / range changes  -> edit a constant here, reflash.
//    2. Live performance changes    -> turn a knob (ranges below define limits).
//
//  Control surface map (also documented in README.md):
//    TOGGLE 1  : MODE select      (UP = Synth, MIDDLE = Granular, DOWN = Generative)
//    TOGGLE 2  : per-mode variant  (e.g. waveform / grain-pitch mode / scale)
//    TOGGLE 3  : FX select         (UP = off, MIDDLE = delay, DOWN = reverb)
//    KNOB 1..6 : per-mode macros   (see each `params::<mode>` block)
//                ... or FX params while editing (hold FOOTSW 1) -- see `params::fx`
//    FOOTSW 1  : tap = engage/bypass | HOLD = edit FX (knobs -> FX, soft-takeover)
//    FOOTSW 2  : mode action       (Granular = freeze, Generative = re-seed)
//    (hold BOTH footswitches 2s  -> reboot to DFU for flashing)
// =============================================================================
#pragma once

namespace params {

// ----------------------------------------------------------------------------
//  Audio engine
// ----------------------------------------------------------------------------
namespace audio {
constexpr int   kBlockSize       = 48;       // samples/channel per callback
// Sample rate is set via SaiHandle config in main.cpp (48 kHz).
}  // namespace audio

// ----------------------------------------------------------------------------
//  MODE 1 -- MIDI synth voice            (osc -> filter -> envelope, USB MIDI)
//    KNOB 1: filter cutoff   KNOB 2: resonance
//    KNOB 3: attack          KNOB 4: decay/release
//    KNOB 5: mod depth (LFO -> cutoff)   KNOB 6: dry/wet of generative mod
//    TOGGLE 2: waveform (UP sine / MID square / DOWN saw)
// ----------------------------------------------------------------------------
namespace synth {
constexpr float kCutoffMinHz   = 40.0f;
constexpr float kCutoffMaxHz   = 12000.0f;
constexpr float kResMin        = 0.0f;
constexpr float kResMax        = 0.92f;     // keep below self-oscillation runaway
constexpr float kAttackMinS    = 0.001f;
constexpr float kAttackMaxS    = 0.50f;
constexpr float kDecayMinS     = 0.02f;
constexpr float kDecayMaxS     = 3.0f;
constexpr float kModDepthMax   = 0.9f;      // fraction of cutoff swept by LFO
}  // namespace synth

// ----------------------------------------------------------------------------
//  MODE 2 -- Granular texture            (records input, sprays grains; freeze)
//    KNOB 1: grain size      KNOB 2: density (grains/sec)
//    KNOB 3: pitch (center)  KNOB 4: pitch spread (randomness)
//    KNOB 5: position scatter KNOB 6: dry/wet mix
//    TOGGLE 2: pitch quantize (UP free / MID octaves / DOWN unison)
//    FOOTSW 2: freeze (stop recording, keep spraying the captured buffer)
// ----------------------------------------------------------------------------
namespace granular {
constexpr float kBufSeconds    = 4.0f;      // SDRAM capture length
constexpr int   kMaxGrains     = 12;        // simultaneous voices
constexpr float kGrainSizeMinMs = 20.0f;
constexpr float kGrainSizeMaxMs = 500.0f;
constexpr float kDensityMinHz  = 1.0f;      // grain spawns per second
constexpr float kDensityMaxHz  = 80.0f;
constexpr float kPitchSemiMin  = -24.0f;    // pitch knob center range
constexpr float kPitchSemiMax  = 24.0f;
constexpr float kPitchSpreadMax = 12.0f;    // +/- semitones of random spread
}  // namespace granular

// ----------------------------------------------------------------------------
//  MODE 3 -- Generative "Krell"          (self-playing, evolving voice)
//    KNOB 1: event rate      KNOB 2: pitch range (random-walk bounds)
//    KNOB 3: tone (cutoff)   KNOB 4: decay length scale
//    KNOB 5: randomness amount KNOB 6: dry/wet of LFO modulation
//    TOGGLE 2: scale (UP chromatic / MID minor pentatonic / DOWN major)
// ----------------------------------------------------------------------------
namespace generative {
constexpr float kRateMinHz     = 0.3f;      // average events per second (slow)
constexpr float kRateMaxHz     = 9.0f;      // ... to fast burble
constexpr float kNoteCenter    = 52.0f;     // MIDI note the walk centers on
constexpr float kNoteRangeMin  = 4.0f;      // +/- semitones at KNOB2 = min
constexpr float kNoteRangeMax  = 30.0f;     // +/- semitones at KNOB2 = max
constexpr float kWalkStepSemi  = 5.0f;      // max jump per event (semitones)
constexpr float kCutoffMinHz   = 120.0f;
constexpr float kCutoffMaxHz   = 9000.0f;
constexpr float kDecayMinS     = 0.05f;
constexpr float kDecayMaxS     = 2.5f;
constexpr float kAttackMinS    = 0.002f;
constexpr float kAttackMaxS    = 0.4f;
}  // namespace generative

// ----------------------------------------------------------------------------
//  Modulation engine        (random LFOs / sample&hold, hardware-RNG seeded)
//    Shared by all modes; depth is set per-mode (usually KNOB 5/6).
// ----------------------------------------------------------------------------
namespace mod {
constexpr float kLfo1MinHz     = 0.02f;     // slow drift
constexpr float kLfo1MaxHz     = 6.0f;
constexpr float kLfo2MinHz     = 0.05f;
constexpr float kLfo2MaxHz     = 12.0f;
constexpr float kSampleHoldHz  = 4.0f;      // default S&H step rate
}  // namespace mod

// ----------------------------------------------------------------------------
//  Non-standard inputs      (see io/sensors.h -- analog input on A0/D15)
//    The analog sensor is read into each mode's Control(). Until it's wired it
//    reads a neutral 0.5 and contributes nothing. (Motion IMU is tier-2.)
// ----------------------------------------------------------------------------
namespace sensors {
constexpr float kAnalogDepth   = 1.0f;      // analog sensor -> destination scale
}  // namespace sensors

// ----------------------------------------------------------------------------
//  Global FX block        (decoupled delay + reverb, processes the active mode)
//    TOGGLE 3 selects: UP = off / MIDDLE = delay / DOWN = reverb.
//    Hold FOOTSWITCH 1 to edit (knobs become FX controls, soft-takeover):
//      KNOB 1 mix   KNOB 2 delay time   KNOB 3 delay feedback
//      KNOB 4 delay tone   KNOB 5 reverb decay   KNOB 6 reverb damping
// ----------------------------------------------------------------------------
namespace fx {
constexpr float kDelayBufSeconds = 2.0f;    // SDRAM delay buffer length per ch
constexpr float kDelayMinMs    = 20.0f;
constexpr float kDelayMaxMs    = 1500.0f;
constexpr float kDelayFbMax    = 0.85f;     // feedback (keep < 1 to decay)
constexpr float kDelayToneMinHz = 600.0f;   // feedback-path lowpass
constexpr float kDelayToneMaxHz = 12000.0f;
constexpr float kRevDecayMin   = 0.60f;     // ReverbSc feedback (tail length)
constexpr float kRevDecayMax   = 0.98f;
constexpr float kRevDampMinHz  = 800.0f;    // ReverbSc LP (HF damping)
constexpr float kRevDampMaxHz  = 18000.0f;
constexpr float kEditHoldMs    = 350.0f;    // footswitch hold time to enter edit
constexpr float kPickupBand    = 0.03f;     // soft-takeover catch window (0..1)
}  // namespace fx

// ----------------------------------------------------------------------------
//  MIDI management interface   (USB MIDI <-> browser WebMIDI tool)
//    See docs/MIDI_PROTOCOL.md for the full contract (CC map + SysEx).
//    Phase 1 (implemented): CC drives the live knob values as "virtual knobs".
// ----------------------------------------------------------------------------
namespace midi {
constexpr int kCcModeKnobBase = 20;  // CC 20..25 -> MODE-layer knobs 1..6
constexpr int kCcFxKnobBase   = 26;  // CC 26..31 -> FX-layer knobs 1..6
}  // namespace midi

}  // namespace params
