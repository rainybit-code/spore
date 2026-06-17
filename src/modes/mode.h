// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Joakim Langkilde
// =============================================================================
//  mode.h  --  common interface every synth mode implements.
// =============================================================================
//  Adding a new mode = implement this interface and slot it into the array in
//  main.cpp. main() handles control reading cadence, MIDI dispatch, and audio.
// =============================================================================
#pragma once

#include "daisy_seed.h"
#include "hothouse.h"
#include "mod/modulation.h"
#include "io/sensors.h"

namespace synthbox {

using daisy::AudioHandle;
using clevelandmusicco::Hothouse;

// Everything a mode might pull from when updating its parameters at control rate.
// (Motion IMU is tier-2 / not wired yet -- see io/imu.h.)
struct ModContext {
  ModEngine&     mod;      // random LFOs / S&H / RNG
  AnalogSensors& sensors;  // analog sensor input (neutral until wired)
  const float*   knob;     // 6 latched MODE-layer knob values (0..1), see io/knobs.h
                           // -- use instead of hw.GetKnobValue so the FX shift-layer
                           //    doesn't disturb mode params. Index with KNOB_1..KNOB_6.
  float          tempoBpm; // current clock tempo (local or MIDI), for tempo-synced LFOs
};

class IMode {
 public:
  virtual ~IMode() {}

  // Called once at startup. hw is provided so modes can bind daisysp::Parameter
  // objects to hw.knobs[...].
  virtual void Init(float sample_rate, Hothouse& hw) = 0;

  // Called when this mode becomes active (e.g. on toggle change). Optional.
  virtual void OnEnter() {}

  // Called once per audio block (control rate): read knobs/toggles + modulation
  // and stage parameters for ProcessBlock.
  virtual void Control(Hothouse& hw, ModContext& ctx) = 0;

  // Fill the output buffer for this block.
  virtual void ProcessBlock(AudioHandle::InputBuffer in,
                            AudioHandle::OutputBuffer out, size_t size) = 0;

  // A footswitch-2 "action" press (mode-specific: freeze / re-seed / ...).
  virtual void Action() {}

  // USB MIDI (only the synth mode uses these; default no-op).
  virtual void NoteOn(float /*note*/, float /*velocity*/) {}
  virtual void NoteOff(float /*note*/) {}
};

}  // namespace synthbox
