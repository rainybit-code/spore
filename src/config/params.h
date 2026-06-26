// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  params.h  --  SINGLE SOURCE OF TRUTH for every tunable value in the synth.
// =============================================================================
//  Anything you might want to tweak, tune, or explore lives here -- grouped by
//  module, with units and sensible ranges in the comments. Change a value,
//  rebuild (`scripts/flash`), and listen. Control ranges and operational
//  thresholds live here rather than as bare literals in the wiring code.
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
//    (DFU: MIDI CC 118 = reflash app / CC 119 = reflash bootloader / Seed BOOT+RESET)
// =============================================================================
#pragma once

#include <cstdint>

namespace params {

// Firmware version (semver, no leading "v"). Reported over SysEx identify so the
// Propagator editor can flag when a newer release is available. Keep in sync with
// the git tag at release time (scripts/release.sh bumps this automatically).
constexpr char kFwVersion[] = "0.4.0";

// ----------------------------------------------------------------------------
//  Audio engine
// ----------------------------------------------------------------------------
namespace audio {
constexpr int kBlockSize = 64;  // samples/channel per callback (~1.3 ms @ 48 kHz)
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
constexpr float kCutoffMinHz = 40.0f;
constexpr float kCutoffMaxHz = 12000.0f;
constexpr float kResMin = 0.0f;
constexpr float kResMax = 0.92f;  // keep below self-oscillation runaway
constexpr float kAttackMinS = 0.001f;
constexpr float kAttackMaxS = 0.50f;
constexpr float kDecayMinS = 0.02f;
constexpr float kDecayMaxS = 3.0f;
constexpr float kModDepthMax = 0.9f;  // fraction of cutoff swept by LFO
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
constexpr float kBufSeconds = 4.0f;  // SDRAM capture length
constexpr int kMaxGrains = 12;       // simultaneous voices
constexpr float kGrainSizeMinMs = 20.0f;
constexpr float kGrainSizeMaxMs = 500.0f;
constexpr float kDensityMinHz = 1.0f;  // grain spawns per second
constexpr float kDensityMaxHz = 80.0f;
constexpr float kPitchSemiMin = -24.0f;  // pitch knob center range
constexpr float kPitchSemiMax = 24.0f;
constexpr float kPitchSpreadMax = 12.0f;  // +/- semitones of random spread
}  // namespace granular

// ----------------------------------------------------------------------------
//  MODE 3 -- Generative "Krell"          (self-playing, evolving voice)
//    KNOB 1: event rate      KNOB 2: pitch range (random-walk bounds)
//    KNOB 3: tone (cutoff)   KNOB 4: decay length scale
//    KNOB 5: randomness amount KNOB 6: dry/wet of LFO modulation
//    TOGGLE 2: scale (UP chromatic / MID minor pentatonic / DOWN major)
// ----------------------------------------------------------------------------
namespace generative {
constexpr float kRateMinHz = 0.3f;      // average events per second (slow)
constexpr float kRateMaxHz = 9.0f;      // ... to fast burble
constexpr float kNoteCenter = 52.0f;    // MIDI note the walk centers on
constexpr float kNoteRangeMin = 4.0f;   // +/- semitones at KNOB2 = min
constexpr float kNoteRangeMax = 30.0f;  // +/- semitones at KNOB2 = max
constexpr float kWalkStepSemi = 5.0f;   // max jump per event (semitones)
constexpr float kCutoffMinHz = 120.0f;
constexpr float kCutoffMaxHz = 9000.0f;
constexpr float kDecayMinS = 0.05f;
constexpr float kDecayMaxS = 2.5f;
constexpr float kAttackMinS = 0.002f;
constexpr float kAttackMaxS = 0.4f;
}  // namespace generative

// ----------------------------------------------------------------------------
//  Modulation engine        (random LFOs / sample&hold, hardware-RNG seeded)
//    Shared by all modes; depth is set per-mode (usually KNOB 5/6).
// ----------------------------------------------------------------------------
namespace mod {
constexpr float kLfo1MinHz = 0.02f;  // slow drift
constexpr float kLfo1MaxHz = 6.0f;
constexpr float kLfo2MinHz = 0.05f;
constexpr float kLfo2MaxHz = 12.0f;
constexpr float kSampleHoldHz = 4.0f;  // default S&H step rate
// Chaos sources (mod/modulation.h): a Lorenz attractor (smooth) + logistic map
// (stepped). Deterministic-but-never-repeating modulation -- the "alive" core.
constexpr float kChaosSpeed = 2.0f;      // Lorenz evolution rate (time-units/sec; higher = busier)
constexpr float kChaosSpeedMin = 0.2f;   // CC 18 maps 0..127 to this range: slow drift ...
constexpr float kChaosSpeedMax = 12.0f;  // ... to busy/turbulent
constexpr float kLogisticHz = 4.0f;      // stepped (logistic-map) chaos step rate
constexpr float kLogisticR = 3.9f;       // logistic map r (chaotic in ~3.57..4.0)
}  // namespace mod

// ----------------------------------------------------------------------------
//  Non-standard inputs      (see io/sensors.h -- analog input on A0/D15)
//    The analog sensor is read into each mode's Control(). Until it's wired it
//    reads a neutral 0.5 and contributes nothing. (Motion IMU is tier-2.)
// ----------------------------------------------------------------------------
namespace sensors {
constexpr float kAnalogDepth = 1.0f;  // analog sensor -> destination scale
}  // namespace sensors

// ----------------------------------------------------------------------------
//  Global FX block        (decoupled delay + reverb, processes the active mode)
//    TOGGLE 3 selects: UP = off / MIDDLE = delay / DOWN = reverb.
//    Hold FOOTSWITCH 1 to edit (knobs become FX controls, soft-takeover):
//      KNOB 1 mix   KNOB 2 delay time   KNOB 3 delay feedback
//      KNOB 4 delay tone   KNOB 5 reverb decay   KNOB 6 reverb damping
// ----------------------------------------------------------------------------
namespace fx {
constexpr float kDelayBufSeconds = 2.0f;  // SDRAM delay buffer length per ch
constexpr float kDelayMinMs = 20.0f;
constexpr float kDelayMaxMs = 1500.0f;
constexpr float kDelayFbMax = 0.85f;       // feedback (keep < 1 to decay)
constexpr float kDelayToneMinHz = 600.0f;  // feedback-path lowpass
constexpr float kDelayToneMaxHz = 12000.0f;
constexpr float kRevDecayMin = 0.60f;  // ReverbSc feedback (tail length)
constexpr float kRevDecayMax = 0.98f;
constexpr float kRevDampMinHz = 800.0f;  // ReverbSc LP (HF damping)
constexpr float kRevDampMaxHz = 18000.0f;
constexpr float kEditHoldMs = 350.0f;  // footswitch hold time to enter edit
constexpr float kPickupBand = 0.03f;   // soft-takeover catch window (0..1)
}  // namespace fx

// ----------------------------------------------------------------------------
//  Master output stage   (fx/master.h: switchable filter + volume on the mix)
//    Applied after the global FX, before the safety limiter. MIDI-controlled.
// ----------------------------------------------------------------------------
namespace master {
constexpr float kCutMinHz = 40.0f;  // filter cutoff range (LP / BP / HP)
constexpr float kCutMaxHz = 18000.0f;
constexpr float kResMax = 0.95f;  // keep below self-oscillation
}  // namespace master

// ----------------------------------------------------------------------------
//  CPU watchdog            (main.cpp: sheds load when the audio callback is hot)
//    Hysteresis on the average callback load: trip after kOverloadBlocks hot
//    blocks, recover after kRecoverBlocks cool ones (1 block = kBlockSize/SR).
// ----------------------------------------------------------------------------
namespace watchdog {
constexpr float kHotLoad = 0.95f;     // avg load above this counts as "hot"
constexpr float kCoolLoad = 0.78f;    // ... and below this as "cool" (recover)
constexpr int kOverloadBlocks = 150;  // ~150 ms hot  -> shed global FX / voices
constexpr int kRecoverBlocks = 400;   // ~400 ms cool -> recover
}  // namespace watchdog

// ----------------------------------------------------------------------------
//  UI feedback             (main.cpp: onboard LED + control-mirror thresholds)
// ----------------------------------------------------------------------------
namespace ui {
constexpr uint32_t kHeartbeatMs = 500;   // onboard LED ~1 Hz alive blink (half period)
constexpr uint32_t kMidiActiveMs = 100;  // hold LED solid this long after a MIDI event
constexpr uint32_t kOverloadMs = 100;    // fast ~5 Hz blink while the watchdog is tripped
constexpr float kEchoDeadband = 0.008f;  // min knob move to mirror back over MIDI
}  // namespace ui

// ----------------------------------------------------------------------------
//  Presets               (io/presets.h: 3 slots per mode, stored in QSPI)
//    Hold FOOTSWITCH 2 to enter preset mode; the Toggle-2 position selects the
//    slot. Flip Toggle 2 to recall a slot; tap FOOTSWITCH 1 (while holding FS2)
//    to save the current sound to the slot at the Toggle-2 position. While held,
//    the LEDs show the active preset: right = 1, left = 2, both = 3.
// ----------------------------------------------------------------------------
namespace preset {
constexpr float kHoldMs = 350.0f;           // FS2 hold time to enter preset mode
constexpr uint32_t kBlinkMs = 200;          // preset-indicator LED blink (half period)
constexpr uint32_t kSaveFlashMs = 500;      // duration of the post-save confirm flash
constexpr uint32_t kSaveFlashBlinkMs = 60;  // fast blink during the save confirm
}  // namespace preset

// ----------------------------------------------------------------------------
//  MIDI management interface   (USB MIDI <-> browser WebMIDI tool)
//    CC drives the live knob values as "virtual knobs"; see docs/MIDI_PROTOCOL.md
//    for the full contract (CC map + SysEx).
// ----------------------------------------------------------------------------
namespace midi {
constexpr int kCcModeKnobBase = 20;  // CC 20..25 -> MODE-layer knobs 1..6
constexpr int kCcFxKnobBase = 26;    // CC 26..31 -> FX-layer knobs 1..6
constexpr int kCcModeSelect =
    16;  // CC 16 -> mode  (0..42 synth / 43..85 granular / 86..127 generative)
constexpr int kCcFxSelect = 17;  // CC 17 -> FX    (0..42 off   / 43..85 delay    / 86..127 reverb)
constexpr int kCcChaosSpeed = 18;  // CC 18 -> Lorenz chaos speed (0..127 -> kChaosSpeedMin..Max)
constexpr int kCcSynthBase = 40;   // CC 40.. -> extended synth params (see config/synth_params.h)
constexpr int kCcTempo = 14;       // CC 14 -> internal clock BPM (0..127 -> 40..200)
constexpr int kCcDelaySync =
    15;  // CC 15 -> delay tempo-sync division (0 off / 1/4 / 1/8 / 1/8. / 1/16)
constexpr int kCcSysReboot = 119;  // CC 119 >=64 -> STM ROM DFU (update the bootloader itself)
constexpr int kCcDaisyReboot =
    118;  // CC 118 >=64 -> Daisy bootloader, infinite DFU (reflash the app)
// Master output stage + control-surface-over-MIDI
constexpr int kCcMasterVol = 7;        // CC 7  -> master volume (standard MIDI volume)
constexpr int kCcMasterFiltType = 88;  // CC 88 -> master filter (0 off / 1 LP / 2 BP / 3 HP)
constexpr int kCcMasterFiltCut = 89;   // CC 89 -> master filter cutoff
constexpr int kCcMasterFiltRes = 90;   // CC 90 -> master filter resonance
constexpr int kCcFootsw1 = 91;         // CC 91 >=64 -> bypass on, <64 -> engaged
constexpr int kCcFootsw2 = 92;         // CC 92 >=64 -> mode action (freeze / re-seed)
constexpr int kCcVar = 93;             // CC 93 -> TOGGLE 2 variant (thirds: 0 / 1 / 2)
constexpr int kCcGenBase = 32;         // CC 32.. -> Generative pod params (see config/gen_params.h)
constexpr int kCcGranBase = 94;        // CC 94.. -> Granular pod params (see config/gran_params.h)
}  // namespace midi

}  // namespace params
